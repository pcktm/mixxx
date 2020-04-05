#include "preferences/dialog/dlgprefeffects.h"

#include "effects/effectsmanager.h"
#include "effects/backends/effectmanifest.h"
#include "effects/backends/effectsbackend.h"

DlgPrefEffects::DlgPrefEffects(QWidget* pParent,
                               UserSettingsPointer pConfig,
                               EffectsManager* pEffectsManager)
        : DlgPreferencePage(pParent),
          m_pConfig(pConfig),
          m_pEffectsManager(pEffectsManager) {
    setupUi(this);

    m_availableEffectsModel.resetFromEffectManager(pEffectsManager);
    for (auto& profile : m_availableEffectsModel.profiles()) {
        EffectManifestPointer pManifest = profile->pManifest;

        // Users are likely to have lots of external plugins installed and
        // many of them are useless for DJing. To avoid cluttering the list
        // shown in WEffectSelector, blacklist external plugins by default.
        bool defaultValue = (pManifest->backendType() == EffectBackendType::BuiltIn);
        bool visible = m_pConfig->getValue<bool>(ConfigKey("[Visible " + pManifest->backendName() + " Effects]",
                                                         pManifest->id()),
                defaultValue);
        profile->bIsVisible = visible;
        m_pEffectsManager->setEffectVisibility(pManifest, visible);
    }
    availableEffectsList->setModel(&m_availableEffectsModel);

    connect(availableEffectsList->selectionModel(),
            SIGNAL(currentRowChanged(const QModelIndex&, const QModelIndex&)),
            this,
            SLOT(availableEffectsListItemSelected(const QModelIndex&)));

    // Highlight first row
    availableEffectsList->selectRow(0);

    availableEffectsList->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    availableEffectsList->setColumnWidth(1, 200);
    availableEffectsList->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);

    chainListWidget->setDragEnabled(true);
    chainListWidget->viewport()->setAcceptDrops(true);
    chainListWidget->setDropIndicatorShown(true);
    chainListWidget->setDragDropMode(QAbstractItemView::InternalMove);
}

DlgPrefEffects::~DlgPrefEffects() {
}

void DlgPrefEffects::slotUpdate() {
    clear();
    m_availableEffectsModel.resetFromEffectManager(m_pEffectsManager);

    if (!m_availableEffectsModel.isEmpty()) {
        availableEffectsList->selectRow(0);
    }

    chainListWidget->clear();
    for (const auto& pChainPreset : m_pEffectsManager->getAvailableChainPresets()) {
        chainListWidget->addItem(pChainPreset->name());
    }
}

void DlgPrefEffects::slotApply() {
    for (EffectProfilePtr profile : m_availableEffectsModel.profiles()) {
        EffectManifestPointer pManifest = profile->pManifest;
        m_pEffectsManager->setEffectVisibility(pManifest, profile->bIsVisible);

        // Effects from different backends can have same Effect IDs.
        // Add backend name to group to uniquely identify those effects.
        // Use untranslated value to keep the group language independent.
        m_pConfig->set(ConfigKey("[Visible " + pManifest->backendName() + " Effects]", pManifest->id()),
                ConfigValue(profile->bIsVisible));
    }

    QStringList chainList;
    for (int i = 0; i < chainListWidget->count(); ++i) {
        chainList << chainListWidget->item(i)->text();
    }
    m_pEffectsManager->setChainPresetOrder(chainList);
}

void DlgPrefEffects::slotResetToDefaults() {
    slotUpdate();
}

void DlgPrefEffects::clear() {
    effectName->clear();
    effectAuthor->clear();
    effectDescription->clear();
    effectVersion->clear();
    effectType->clear();
}

void DlgPrefEffects::availableEffectsListItemSelected(const QModelIndex& selected) {
    QString effectId = m_availableEffectsModel.data(selected, Qt::UserRole).toString();

    if (effectId == QVariant().toString())
        return;

    EffectManifestPointer pManifest = m_pEffectsManager->getManifestFromUniqueId(effectId);

    effectName->setText(pManifest->name());
    effectAuthor->setText(pManifest->author());
    effectDescription->setText(pManifest->description());
    effectVersion->setText(pManifest->version());
    effectType->setText(pManifest->translatedBackendName());
}
