#pragma once

#include <QWidget>
#include <QString>

class QToolButton;
class QLayout;

namespace dracoved {

// A lightweight disclosure widget: a clickable header that shows/hides a content
// area. Used to tuck rarely-needed "advanced" controls away while keeping the
// common controls visible. Child controls live in contentWidget(), so existing
// member-pointer wiring is unaffected.
class CollapsibleSection : public QWidget {
    Q_OBJECT

public:
    explicit CollapsibleSection(const QString& title, bool expanded = false, QWidget* parent = nullptr);

    void setContentLayout(QLayout* layout);
    QWidget* contentWidget() const { return content_; }
    void setExpanded(bool expanded);
    bool isExpanded() const;

private:
    QToolButton* toggle_ = nullptr;
    QWidget* content_ = nullptr;
};

}  // namespace dracoved
