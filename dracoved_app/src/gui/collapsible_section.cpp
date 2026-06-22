#include "collapsible_section.h"

#include <QLayout>
#include <QToolButton>
#include <QVBoxLayout>

namespace dracoved {

CollapsibleSection::CollapsibleSection(const QString& title, bool expanded, QWidget* parent)
    : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(2);

    toggle_ = new QToolButton(this);
    toggle_->setObjectName("collapsibleHeader");
    toggle_->setText(title);
    toggle_->setCheckable(true);
    toggle_->setChecked(expanded);
    toggle_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toggle_->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
    toggle_->setCursor(Qt::PointingHandCursor);
    toggle_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    toggle_->setStyleSheet(
        "QToolButton#collapsibleHeader { text-align: left; padding: 3px 4px; border: none;"
        "  font-weight: 600; background: transparent; }");

    content_ = new QWidget(this);
    content_->setVisible(expanded);

    outer->addWidget(toggle_);
    outer->addWidget(content_);

    connect(toggle_, &QToolButton::toggled, this, [this](bool on) {
        content_->setVisible(on);
        toggle_->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
    });
}

void CollapsibleSection::setContentLayout(QLayout* layout) {
    if (!content_) {
        return;
    }
    if (content_->layout()) {
        delete content_->layout();
    }
    content_->setLayout(layout);
}

void CollapsibleSection::setExpanded(bool expanded) {
    if (toggle_) {
        toggle_->setChecked(expanded);
    }
}

bool CollapsibleSection::isExpanded() const {
    return toggle_ && toggle_->isChecked();
}

}  // namespace dracoved
