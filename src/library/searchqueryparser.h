#pragma once

#include <QRegularExpression>
#include <QString>
#include <QtSql>
#include <memory>

#include "library/searchquery.h"
#include "library/trackcollection.h"
#include "util/class.h"

class SearchQueryParser {
  public:
    explicit SearchQueryParser(TrackCollection* pTrackCollection, QStringList searchColumns);

    void setSearchColumns(QStringList searchColumns);

    std::unique_ptr<QueryNode> parseQuery(
            const QString& query,
            const QString& extraFilter) const;

    /// splits the query into a list of terms
    static QStringList splitQueryIntoWords(const QString& query);
    /// checks if the changed search query is less specific then the original term
    static bool queryIsLessSpecific(const QString& original, const QString& changed);

  private:
    void parseTokens(QStringList tokens,
                     AndNode* pQuery) const;

    std::unique_ptr<AndNode> parseAndNode(const QString& query) const;
    std::unique_ptr<OrNode> parseOrNode(const QString& query) const;

    struct TextArgumentResult {
        QString argument;
        StringMatch mode;
    };

    TextArgumentResult getTextArgument(QString argument,
            QStringList* tokens) const;

    TrackCollection* m_pTrackCollection;
    QStringList m_queryColumns;
    bool m_searchCrates;
    QStringList m_textFilters;
    QStringList m_numericFilters;
    QStringList m_specialFilters;
    QStringList m_allFilters;
    QHash<QString, QStringList> m_fieldToSqlColumns;

    QRegularExpression m_fuzzyMatcher;
    QRegularExpression m_textFilterMatcher;
    QRegularExpression m_crateFilterMatcher;
    QRegularExpression m_numericFilterMatcher;
    QRegularExpression m_specialFilterMatcher;

    DISALLOW_COPY_AND_ASSIGN(SearchQueryParser);
};
