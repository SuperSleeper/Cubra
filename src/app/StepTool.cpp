#include "app/StepTool.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

StepTool::StepTool(QWidget* host) : QObject(host), host_(host) {
    bar_ = new QFrame(host_);
    bar_->setObjectName("stepBar");
    bar_->setFrameShape(QFrame::StyledPanel);
    bar_->setAutoFillBackground(true);
    bar_->hide();

    auto* layout = new QVBoxLayout(bar_);
    layout->setContentsMargins(10, 6, 10, 6);
    layout->setSpacing(4);
    title_ = new QLabel(bar_);
    QFont bold = title_->font();
    bold.setBold(true);
    title_->setFont(bold);
    layout->addWidget(title_);
    note_ = new QLabel(bar_);
    note_->setObjectName("stepNote");
    note_->setWordWrap(true);
    note_->setMaximumWidth(520);
    note_->setStyleSheet("color: #b8c4d6;");
    note_->hide();
    layout->addWidget(note_);

    auto* row = new QHBoxLayout;
    slots_ = new QVBoxLayout;
    slots_->setSpacing(2);
    row->addLayout(slots_, 1);
    auto* buttons = new QVBoxLayout;
    apply_ = new QPushButton(tr("Apply (Enter)"), bar_);
    apply_->setObjectName("stepApply");
    apply_->setFocusPolicy(Qt::NoFocus);
    auto* cancel = new QPushButton(tr("Cancel (Esc)"), bar_);
    cancel->setFocusPolicy(Qt::NoFocus);
    buttons->addWidget(apply_);
    buttons->addWidget(cancel);
    row->addLayout(buttons);
    layout->addLayout(row);

    message_ = new QLabel(bar_);
    message_->setObjectName("stepMessage");
    message_->setWordWrap(true);
    layout->addWidget(message_);

    connect(apply_, &QPushButton::clicked, this, &StepTool::confirm);
    connect(cancel, &QPushButton::clicked, this, &StepTool::cancel);
}

void StepTool::begin(const QString& id, const QString& title, std::vector<Step> steps, bool confirm) {
    for (QPushButton* button : slotButtons_) delete button;
    slotButtons_.clear();

    active_ = true;
    id_ = id;
    confirm_ = confirm;
    steps_ = std::move(steps);
    picks_.assign(steps_.size(), {});
    title_->setText(title);
    message_->clear();
    note_->clear();
    note_->hide();
    for (int i = 0; i < int(steps_.size()); ++i) {
        auto* button = new QPushButton(bar_);
        button->setObjectName(QStringLiteral("stepSlot%1").arg(i));
        button->setCheckable(true);
        button->setFocusPolicy(Qt::NoFocus);
        button->setStyleSheet("text-align: left; padding: 3px 8px;");
        slots_->addWidget(button);
        slotButtons_.push_back(button);
        connect(button, &QPushButton::clicked, this, [this, i] { focusStep(i); });
    }
    apply_->setVisible(confirm_);
    focusStep(0);
    bar_->show();
    bar_->raise();
}

void StepTool::cancel() {
    if (!active_) return;
    active_ = false;
    bar_->hide();
    emit cancelled();
}

void StepTool::preset(int step, std::vector<Choice> choices) {
    picks_[step] = std::move(choices);
    // Carry on from the first step that still needs something.
    int next = 0;
    while (next < int(steps_.size()) - 1 && !picks_[next].empty()) ++next;
    focusStep(next);
}

void StepTool::offer(const Choice& choice) {
    const Step& step = steps_[current_];
    const auto picksBodies = [](Pick pick) { return pick == Pick::Body || pick == Pick::Bodies; };
    if (picksBodies(step.pick)) {
        // A solid cannot be both the thing worked on and the tool.
        for (int other = 0; other < int(picks_.size()); ++other) {
            if (other == current_) continue;
            auto& list = picks_[other];
            list.erase(std::remove_if(list.begin(), list.end(), [&](const Choice& c) { return c.body == choice.body && picksBodies(steps_[other].pick); }), list.end());
        }
    }

    auto& list = picks_[current_];
    if (step.pick == Pick::Bodies) {
        const auto found = std::find_if(list.begin(), list.end(), [&](const Choice& c) { return c.body == choice.body; });
        if (found != list.end()) list.erase(found);
        else list.push_back(choice);
        refresh();
        return;
    }

    list = {choice};
    if (current_ + 1 < int(steps_.size())) {
        focusStep(current_ + 1);
        return;
    }
    if (!confirm_) {
        confirm();
        return;
    }
    refresh();
}

bool StepTool::ready() const {
    return std::all_of(picks_.begin(), picks_.end(), [](const auto& list) { return !list.empty(); });
}

bool StepTool::confirm() {
    if (!active_) return false;
    if (!ready()) {
        setMessage(tr("Pick everything listed first."));
        return false;
    }
    active_ = false;
    bar_->hide();
    const auto picks = picks_;
    emit finished(id_, picks);
    return true;
}

void StepTool::setNote(const QString& note) {
    note_->setText(note);
    note_->setVisible(!note.isEmpty());
    refresh();
}

void StepTool::setMessage(const QString& message) {
    message_->setText(message);
    refresh();
}

void StepTool::focusStep(int step) {
    current_ = std::clamp(step, 0, int(steps_.size()) - 1);
    refresh();
}

void StepTool::refresh() {
    for (int i = 0; i < int(steps_.size()); ++i) {
        QStringList names;
        for (const Choice& c : picks_[i]) names << c.name;
        const QString chosen = names.isEmpty() ? (i == current_ ? tr("click on the canvas") : tr("not chosen")) : names.join(QStringLiteral(", "));
        slotButtons_[i]->setText(QStringLiteral("%1 %2:  %3").arg(i == current_ ? QStringLiteral("▶") : QStringLiteral("  "), steps_[i].label, chosen));
        slotButtons_[i]->setChecked(i == current_);
    }
    apply_->setEnabled(ready());
    message_->setVisible(!message_->text().isEmpty());
    bar_->adjustSize();
    const int width = std::max(bar_->sizeHint().width(), 380);
    bar_->setGeometry((host_->width() - width) / 2, 10, width, bar_->sizeHint().height());
    emit changed();
}
