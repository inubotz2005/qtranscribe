#include "NavigationModel.h"

using namespace Qt::StringLiterals;

NavigationModel::NavigationModel(QObject* parent)
    : QAbstractListModel(parent) {
    rebuildItems();
}

int NavigationModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(m_items.size());
}

QVariant NavigationModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(m_items.size())) {
        return {};
    }

    const auto& item = m_items.at(index.row());
    switch (role) {
        case SectionRole:
            return item.section;
        case TitleRole:
            return item.title;
        case IconSourceRole:
            return item.iconSource;
        case SectionIdRole:
            return item.sectionId;
        case IsFirstInSectionRole:
            return item.isFirstInSection;
        default:
            return {};
    }
}

QHash<int, QByteArray> NavigationModel::roleNames() const {
    return {{SectionRole, "section"},
            {TitleRole, "title"},
            {IconSourceRole, "iconSource"},
            {SectionIdRole, "sectionId"},
            {IsFirstInSectionRole, "isFirstInSection"}};
}

bool NavigationModel::isOnline() const {
    return m_isOnline;
}

void NavigationModel::setIsOnline(bool online) {
    if (m_isOnline == online) {
        return;
    }
    m_isOnline = online;
    rebuildItems();
    emit isOnlineChanged();
}

int NavigationModel::count() const {
    return static_cast<int>(m_items.size());
}

void NavigationModel::rebuildItems() {
    beginResetModel();
    m_items.clear();

    m_items.append({u"MAIN"_s, tr("Dictate"), u"qrc:/qt/qml/QTranscribe/assets/icons/mic.svg"_s, u"dictate"_s, true});

    m_items.append(
        {u"MAIN"_s, tr("History"), u"qrc:/qt/qml/QTranscribe/assets/icons/history.svg"_s, u"history"_s, false});

    if (m_isOnline) {
        m_items.append({u"MAIN"_s, tr("Cloud Usage"), u"qrc:/qt/qml/QTranscribe/assets/icons/activity.svg"_s,
                        u"cloudUsage"_s, false});
    }

    m_items.append({u"PREFERENCES"_s, tr("Dictation"), u"qrc:/qt/qml/QTranscribe/assets/icons/speech.svg"_s,
                    m_isOnline ? u"online"_s : u"offline"_s, true});

    m_items.append({u"PREFERENCES"_s, tr("System & Audio"), u"qrc:/qt/qml/QTranscribe/assets/icons/keyboard.svg"_s,
                    u"system"_s, false});

    m_items.append({u"INFO"_s, tr("About"), u"qrc:/qt/qml/QTranscribe/assets/icons/info.svg"_s, u"about"_s, true});

    m_items.append(
        {u"INFO"_s, tr("License"), u"qrc:/qt/qml/QTranscribe/assets/icons/license.svg"_s, u"license"_s, false});

    endResetModel();
    emit countChanged();
}
