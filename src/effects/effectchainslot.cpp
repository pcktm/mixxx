#include "effects/effectchainslot.h"

#include "control/controlencoder.h"
#include "control/controlpotmeter.h"
#include "control/controlpushbutton.h"
#include "effects/effectslot.h"
#include "effects/effectsmanager.h"
#include "effects/effectsmessenger.h"
#include "effects/presets/effectchainpresetmanager.h"
#include "engine/effects/engineeffectchain.h"
#include "engine/engine.h"
#include "mixer/playermanager.h"
#include "util/defs.h"
#include "util/math.h"
#include "util/sample.h"
#include "util/xml.h"

EffectChainSlot::EffectChainSlot(const QString& group,
        EffectsManager* pEffectsManager,
        EffectsMessengerPointer pEffectsMessenger,
        SignalProcessingStage stage)
        : // The control group names are 1-indexed while internally everything
          // is 0-indexed.
          m_presetName(""),
          m_pEffectsManager(pEffectsManager),
          m_pChainPresetManager(pEffectsManager->getChainPresetManager()),
          m_pMessenger(pEffectsMessenger),
          m_group(group),
          m_signalProcessingStage(stage),
          m_pEngineEffectChain(nullptr) {
    // qDebug() << "EffectChainSlot::EffectChainSlot " << group << ' ' << iChainNumber;

    m_pControlClear = std::make_unique<ControlPushButton>(ConfigKey(m_group, "clear"));
    connect(m_pControlClear.get(),
            &ControlObject::valueChanged,
            this,
            &EffectChainSlot::slotControlClear);

    m_pControlNumEffectSlots = std::make_unique<ControlObject>(
            ConfigKey(m_group, "num_effectslots"));
    m_pControlNumEffectSlots->setReadOnly();

    m_pControlChainLoaded =
            std::make_unique<ControlObject>(ConfigKey(m_group, "loaded"));
    m_pControlChainLoaded->setReadOnly();
    if (group != QString()) {
        m_pControlChainLoaded->forceSet(1.0);
    }

    m_pControlChainEnabled =
            std::make_unique<ControlPushButton>(ConfigKey(m_group, "enabled"));
    m_pControlChainEnabled->setButtonMode(ControlPushButton::POWERWINDOW);
    // Default to enabled. The skin might not show these buttons.
    m_pControlChainEnabled->setDefaultValue(true);
    m_pControlChainEnabled->set(true);
    connect(m_pControlChainEnabled.get(),
            &ControlObject::valueChanged,
            this,
            &EffectChainSlot::sendParameterUpdate);

    m_pControlChainMix = std::make_unique<ControlPotmeter>(
            ConfigKey(m_group, "mix"), 0.0, 1.0, false, true, false, true, 1.0);
    m_pControlChainMix->set(static_cast<int>(EffectChainMixMode::DrySlashWet));
    connect(m_pControlChainMix.get(),
            &ControlObject::valueChanged,
            this,
            &EffectChainSlot::sendParameterUpdate);

    m_pControlChainSuperParameter = std::make_unique<ControlPotmeter>(
            ConfigKey(m_group, "super1"), 0.0, 1.0);
    // QObject::connect cannot connect to slots with optional parameters using function
    // pointer syntax if the slot has more parameters than the signal, so use a lambda
    // to hack around this limitation.
    connect(m_pControlChainSuperParameter.get(),
            &ControlObject::valueChanged,
            this,
            [=](double value) { slotControlChainSuperParameter(value, false); });
    m_pControlChainSuperParameter->set(0.0);
    m_pControlChainSuperParameter->setDefaultValue(0.0);

    m_pControlChainMixMode =
            std::make_unique<ControlPushButton>(ConfigKey(m_group, "mix_mode"));
    m_pControlChainMixMode->setButtonMode(ControlPushButton::TOGGLE);
    m_pControlChainMixMode->setStates(
            static_cast<int>(EffectChainMixMode::NumMixModes));
    connect(m_pControlChainMixMode.get(),
            &ControlObject::valueChanged,
            this,
            &EffectChainSlot::sendParameterUpdate);

    m_pControlLoadPreset = std::make_unique<ControlObject>(
            ConfigKey(m_group, "load_preset"), false);
    connect(m_pControlLoadPreset.get(),
            &ControlObject::valueChanged,
            this,
            &EffectChainSlot::slotControlLoadChainPreset);

    m_pControlLoadedPreset = std::make_unique<ControlObject>(
            ConfigKey(m_group, "loaded_preset"));
    m_pControlLoadedPreset->setReadOnly();

    m_pControlChainNextPreset = std::make_unique<ControlPushButton>(
            ConfigKey(m_group, "next_chain"));
    connect(m_pControlChainNextPreset.get(),
            &ControlObject::valueChanged,
            this,
            &EffectChainSlot::slotControlChainNextPreset);

    m_pControlChainPrevPreset = std::make_unique<ControlPushButton>(
            ConfigKey(m_group, "prev_chain"));
    connect(m_pControlChainPrevPreset.get(),
            &ControlObject::valueChanged,
            this,
            &EffectChainSlot::slotControlChainPrevPreset);

    // Ignoring no-ops is important since this is for +/- tickers.
    m_pControlChainSelector = std::make_unique<ControlEncoder>(
            ConfigKey(m_group, "chain_selector"), false);
    connect(m_pControlChainSelector.get(),
            &ControlObject::valueChanged,
            this,
            &EffectChainSlot::slotControlChainSelector);

    // ControlObjects for skin <-> controller mapping interaction.
    // Refer to comment in header for full explanation.
    m_pControlChainShowFocus = std::make_unique<ControlPushButton>(
            ConfigKey(m_group, "show_focus"));
    m_pControlChainShowFocus->setButtonMode(ControlPushButton::TOGGLE);

    m_pControlChainHasControllerFocus = std::make_unique<ControlPushButton>(
            ConfigKey(m_group, "controller_input_active"));
    m_pControlChainHasControllerFocus->setButtonMode(ControlPushButton::TOGGLE);

    m_pControlChainShowParameters = std::make_unique<ControlPushButton>(
            ConfigKey(m_group, "show_parameters"),
            true);
    m_pControlChainShowParameters->setButtonMode(ControlPushButton::TOGGLE);

    m_pControlChainFocusedEffect = std::make_unique<ControlPushButton>(
            ConfigKey(m_group, "focused_effect"),
            true);
    m_pControlChainFocusedEffect->setButtonMode(ControlPushButton::TOGGLE);

    addToEngine();
}

EffectChainSlot::~EffectChainSlot() {
    m_effectSlots.clear();
    removeFromEngine();
}

void EffectChainSlot::addToEngine() {
    m_pEngineEffectChain = new EngineEffectChain(
            m_group,
            m_pEffectsManager->registeredInputChannels(),
            m_pEffectsManager->registeredOutputChannels());
    EffectsRequest* pRequest = new EffectsRequest();
    pRequest->type = EffectsRequest::ADD_EFFECT_CHAIN;
    pRequest->AddEffectChain.signalProcessingStage = m_signalProcessingStage;
    pRequest->AddEffectChain.pChain = m_pEngineEffectChain;
    m_pMessenger->writeRequest(pRequest);

    sendParameterUpdate();
}

void EffectChainSlot::removeFromEngine() {
    VERIFY_OR_DEBUG_ASSERT(m_effectSlots.isEmpty()) {
        m_effectSlots.clear();
    }

    EffectsRequest* pRequest = new EffectsRequest();
    pRequest->type = EffectsRequest::REMOVE_EFFECT_CHAIN;
    pRequest->RemoveEffectChain.signalProcessingStage = m_signalProcessingStage;
    pRequest->RemoveEffectChain.pChain = m_pEngineEffectChain;
    m_pMessenger->writeRequest(pRequest);

    m_pEngineEffectChain = nullptr;
}

const QString& EffectChainSlot::presetName() const {
    return m_presetName;
}

void EffectChainSlot::setPresetName(const QString& name) {
    m_presetName = name;
    emit nameChanged(name);
}

void EffectChainSlot::loadChainPreset(EffectChainPresetPointer pPreset) {
    VERIFY_OR_DEBUG_ASSERT(pPreset) {
        return;
    }
    slotControlClear(1);

    int effectSlotIndex = 0;
    for (const auto& pEffectPreset : pPreset->effectPresets()) {
        EffectSlotPointer pEffectSlot = m_effectSlots.at(effectSlotIndex);
        if (pEffectPreset->isEmpty()) {
            pEffectSlot->loadEffectFromPreset(nullptr);
            effectSlotIndex++;
            continue;
        }
        pEffectSlot->loadEffectFromPreset(pEffectPreset);
        effectSlotIndex++;
    }

    setMixMode(pPreset->mixMode());
    m_pControlChainSuperParameter->setDefaultValue(pPreset->superKnob());

    setPresetName(pPreset->name());
    m_pControlLoadedPreset->setAndConfirm(presetIndex());
}

void EffectChainSlot::sendParameterUpdate() {
    EffectsRequest* pRequest = new EffectsRequest();
    pRequest->type = EffectsRequest::SET_EFFECT_CHAIN_PARAMETERS;
    pRequest->pTargetChain = m_pEngineEffectChain;
    pRequest->SetEffectChainParameters.enabled = m_pControlChainEnabled->get();
    pRequest->SetEffectChainParameters.mix_mode = mixMode();
    pRequest->SetEffectChainParameters.mix = m_pControlChainMix->get();
    m_pMessenger->writeRequest(pRequest);
}

QString EffectChainSlot::group() const {
    return m_group;
}

double EffectChainSlot::getSuperParameter() const {
    return m_pControlChainSuperParameter->get();
}

void EffectChainSlot::setSuperParameter(double value, bool force) {
    m_pControlChainSuperParameter->set(value);
    slotControlChainSuperParameter(value, force);
}

EffectChainMixMode EffectChainSlot::mixMode() const {
    return static_cast<EffectChainMixMode>(
            static_cast<int>(
                    m_pControlChainMixMode->get()));
}

void EffectChainSlot::setMixMode(EffectChainMixMode mixMode) {
    m_pControlChainMixMode->set(static_cast<int>(mixMode));
    sendParameterUpdate();
}

EffectSlotPointer EffectChainSlot::addEffectSlot(const QString& group) {
    if (kEffectDebugOutput) {
        qDebug() << debugString() << "addEffectSlot" << group;
    }
    EffectSlotPointer pEffectSlot = EffectSlotPointer(new EffectSlot(group,
            m_pEffectsManager,
            m_pMessenger,
            m_effectSlots.size(),
            this,
            m_pEngineEffectChain));

    m_effectSlots.append(pEffectSlot);
    int numEffectSlots = m_pControlNumEffectSlots->get() + 1;
    m_pControlNumEffectSlots->forceSet(numEffectSlots);
    m_pControlChainFocusedEffect->setStates(numEffectSlots);
    return pEffectSlot;
}

void EffectChainSlot::registerInputChannel(const ChannelHandleAndGroup& handleGroup,
        const double initialValue) {
    VERIFY_OR_DEBUG_ASSERT(!m_channelEnableButtons.contains(handleGroup)) {
        return;
    }

    auto pEnableControl = std::make_shared<ControlPushButton>(
            ConfigKey(m_group, QString("group_%1_enable").arg(handleGroup.name())),
            true,
            initialValue);
    m_channelEnableButtons.insert(handleGroup, pEnableControl);
    pEnableControl->setButtonMode(ControlPushButton::POWERWINDOW);
    if (pEnableControl->toBool()) {
        enableForInputChannel(handleGroup);
    }

    connect(pEnableControl.get(),
            &ControlObject::valueChanged,
            this,
            [this, handleGroup](double value) { slotChannelStatusChanged(value, handleGroup); });
}

EffectSlotPointer EffectChainSlot::getEffectSlot(unsigned int slotNumber) {
    //qDebug() << debugString() << "getEffectSlot" << slotNumber;
    VERIFY_OR_DEBUG_ASSERT(slotNumber <= static_cast<unsigned int>(m_effectSlots.size())) {
        return EffectSlotPointer();
    }
    return m_effectSlots[slotNumber];
}

void EffectChainSlot::slotControlClear(double v) {
    for (EffectSlotPointer pEffectSlot : m_effectSlots) {
        pEffectSlot->slotClear(v);
    }
}

void EffectChainSlot::slotControlChainSuperParameter(double v, bool force) {
    // qDebug() << debugString() << "slotControlChainSuperParameter" << v;

    m_pControlChainSuperParameter->set(v);
    for (const auto& pEffectSlot : m_effectSlots) {
        pEffectSlot->setMetaParameter(v, force);
    }
}

void EffectChainSlot::slotControlChainSelector(double value) {
    int index = presetIndex();
    if (value > 0) {
        index++;
    } else {
        index--;
    }
    loadChainPreset(presetAtIndex(index));
}

void EffectChainSlot::slotControlLoadChainPreset(double value) {
    // subtract 1 to make the ControlObject 1-indexed like other ControlObjects
    loadChainPreset(presetAtIndex(value - 1));
}

void EffectChainSlot::slotControlChainNextPreset(double value) {
    if (value > 0) {
        loadChainPreset(presetAtIndex(presetIndex() + 1));
    }
}

void EffectChainSlot::slotControlChainPrevPreset(double value) {
    if (value > 0) {
        loadChainPreset(m_pChainPresetManager->presetAtIndex(presetIndex() - 1));
    }
}

void EffectChainSlot::slotChannelStatusChanged(
        double value, const ChannelHandleAndGroup& handleGroup) {
    if (value > 0) {
        enableForInputChannel(handleGroup);
    } else {
        disableForInputChannel(handleGroup);
    }
}

void EffectChainSlot::enableForInputChannel(const ChannelHandleAndGroup& handleGroup) {
    if (m_enabledInputChannels.contains(handleGroup)) {
        return;
    }

    EffectsRequest* request = new EffectsRequest();
    request->type = EffectsRequest::ENABLE_EFFECT_CHAIN_FOR_INPUT_CHANNEL;
    request->pTargetChain = m_pEngineEffectChain;
    request->EnableInputChannelForChain.pChannelHandle = &handleGroup.handle();

    // Allocate EffectStates here in the main thread to avoid allocating
    // memory in the realtime audio callback thread. Pointers to the
    // EffectStates are passed to the EffectRequest and the EffectProcessorImpls
    // store the pointers. The containers of EffectState* pointers get deleted
    // by ~EffectsRequest, but the EffectStates are managed by EffectProcessorImpl.

    // The EffectStates for one EngineEffectChain must be sent all together in
    // the same message using an EffectStatesMapArray. If they were separated
    // into a message for each effect, there would be a chance that the
    // EngineEffectChain could get activated in one cycle of the audio callback
    // thread but the EffectStates for an EngineEffect would not be received by
    // EngineEffectsManager until the next audio callback cycle.

    auto pEffectStatesMapArray = new EffectStatesMapArray;
    for (int i = 0; i < m_effectSlots.size(); ++i) {
        m_effectSlots[i]->fillEffectStatesMap(&(*pEffectStatesMapArray)[i]);
    }
    request->EnableInputChannelForChain.pEffectStatesMapArray = pEffectStatesMapArray;

    m_pMessenger->writeRequest(request);

    m_enabledInputChannels.insert(handleGroup);
}

void EffectChainSlot::disableForInputChannel(const ChannelHandleAndGroup& handleGroup) {
    if (!m_enabledInputChannels.remove(handleGroup)) {
        return;
    }

    EffectsRequest* request = new EffectsRequest();
    request->type = EffectsRequest::DISABLE_EFFECT_CHAIN_FOR_INPUT_CHANNEL;
    request->pTargetChain = m_pEngineEffectChain;
    request->DisableInputChannelForChain.pChannelHandle = &handleGroup.handle();
    m_pMessenger->writeRequest(request);
}

int EffectChainSlot::presetIndex() const {
    return m_pChainPresetManager->presetIndex(m_presetName);
}

EffectChainPresetPointer EffectChainSlot::presetAtIndex(int index) const {
    return m_pChainPresetManager->presetAtIndex(index);
}
