// SPDX-FileCopyrightText: 2026 Samer Ali
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "utils/UtilsGlobal.hpp"

#include <QtCore/QMap>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QPointer>
#include <QtWidgets/QWidget>

class QLabel;
class QLineEdit;
class QToolButton;
class QVBoxLayout;
class QHBoxLayout;
class QMenu;
class QFrame;

namespace Utils {

class UTILS_EXPORT SidebarPanelFrame final : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(QString title READ title WRITE setTitle NOTIFY titleChanged)
    Q_PROPERTY(QStringList viewOptions READ viewOptions WRITE setViewOptions NOTIFY viewOptionsChanged)
    Q_PROPERTY(QString subtitle READ subtitle WRITE setSubtitle NOTIFY subtitleChanged)
    Q_PROPERTY(bool searchEnabled READ searchEnabled WRITE setSearchEnabled NOTIFY searchEnabledChanged)
    Q_PROPERTY(QString searchText READ searchText WRITE setSearchText NOTIFY searchTextChanged)
    Q_PROPERTY(QString searchPlaceholder READ searchPlaceholder WRITE setSearchPlaceholder NOTIFY searchPlaceholderChanged)
    Q_PROPERTY(bool headerDividerVisible READ headerDividerVisible WRITE setHeaderDividerVisible NOTIFY headerDividerVisibleChanged)

public:
    explicit SidebarPanelFrame(QWidget* parent = nullptr);

    QString title() const;
    void setTitle(const QString& title);

    QStringList viewOptions() const;
    void setViewOptions(const QStringList& options);

    QString subtitle() const;
    void setSubtitle(const QString& subtitle);

    bool searchEnabled() const;
    void setSearchEnabled(bool enabled);

    QString searchText() const;
    void setSearchText(const QString& text);

    QString searchPlaceholder() const;
    void setSearchPlaceholder(const QString& text);

    bool headerDividerVisible() const;
    void setHeaderDividerVisible(bool visible);

    void setContentWidget(QWidget* widget);
    QWidget* contentWidget() const;
    QLineEdit* searchField() const;

    void addAction(const QString& id, const QIcon& icon, const QString& tooltip = QString());
    QToolButton* actionButton(const QString& id) const;
    void setActionVisible(const QString& id, bool visible);
    void clearActions();

signals:
    void titleChanged(const QString& title);
    void subtitleChanged(const QString& subtitle);
    void viewOptionsChanged(const QStringList& options);
    void viewSelected(const QString& viewId);
    void searchEnabledChanged(bool enabled);
    void searchTextChanged(const QString& text);
    void searchPlaceholderChanged(const QString& text);
    void actionTriggered(const QString& id);
    void headerDividerVisibleChanged(bool visible);

private slots:
    void handleAction();
    void handleSearchTextChanged(const QString& text);
    void handleViewTriggered(QAction* action);

private:
    void updateHeader();
    void updateSearchVisibility();

    QString m_title;
    QString m_subtitle;

    QToolButton* m_viewButton = nullptr;
    QMenu* m_viewMenu = nullptr;
    QLabel* m_subtitleLabel = nullptr;
    QWidget* m_headerWidget = nullptr;
    QHBoxLayout* m_actionLayout = nullptr;

    QLineEdit* m_search = nullptr;
    QFrame* m_headerDivider = nullptr;
    QWidget* m_content = nullptr;
    QVBoxLayout* m_contentLayout = nullptr;

    bool m_searchEnabled = true;
    bool m_blockSearchSignal = false;
    bool m_headerDividerVisible = true;

    QMap<QString, QToolButton*> m_actions;
    QStringList m_viewOptions;
};

} // namespace Utils
