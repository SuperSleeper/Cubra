#include "app/FeaturePanel.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>

using td::Feature;
using td::Op;
using td::Shape;

namespace {

constexpr unsigned kBox = 1, kCylinder = 2, kSphere = 4, kTorus = 8, kWedge = 16, kPrisms = 32, kStar = 64, kGear = 128, kCoil = 256, kCapsule = 512, kCurve = 1024, kAll = 2047;

unsigned maskOf(Shape shape) {
    switch (shape) {
        case Shape::Box: return kBox;
        case Shape::Cylinder: return kCylinder;
        case Shape::Sphere: return kSphere;
        case Shape::Torus: return kTorus;
        case Shape::Wedge: return kWedge;
        case Shape::Star: return kStar;
        case Shape::Gear: return kGear;
        case Shape::Coil: return kCoil;
        case Shape::Capsule: return kCapsule;
        case Shape::Curve: return kCurve;
        case Shape::Prisms:
        case Shape::Compound:
        case Shape::Mesh: return kPrisms;
    }
    return 0;
}

QString listLabel(const FeaturePanel::Row& row) {
    const QString name = row.feature.name.isEmpty() ? td::shapeName(row.feature.shape) : row.feature.name;
    return row.depth == 0 ? name : QString(row.depth * 4, ' ') + QStringLiteral("\u25b8 ") + name;
}

}  // namespace

FeaturePanel::FeaturePanel(QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);

    list_ = new QListWidget(this);
    list_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    root->addWidget(list_, 1);
    connect(list_, &QListWidget::itemSelectionChanged, this, [this] {
        if (loading_) return;
        loadSelected();
        emit selectionChanged();
    });

    auto* rowButtons = new QHBoxLayout;
    upButton_ = new QPushButton(tr("Up"), this);
    downButton_ = new QPushButton(tr("Down"), this);
    removeButton_ = new QPushButton(tr("Delete"), this);
    rowButtons->addWidget(upButton_);
    rowButtons->addWidget(downButton_);
    rowButtons->addWidget(removeButton_);
    root->addLayout(rowButtons);
    connect(upButton_, &QPushButton::clicked, this, [this] { emit moveRequested(selected(), -1); });
    connect(downButton_, &QPushButton::clicked, this, [this] { emit moveRequested(selected(), 1); });
    connect(removeButton_, &QPushButton::clicked, this, [this] { emit removeRequested(selectedRows()); });

    form_ = new QWidget(this);
    formLayout_ = new QFormLayout(form_);
    formLayout_->setContentsMargins(0, 0, 0, 0);
    root->addWidget(form_);

    name_ = new QLineEdit(form_);
    formLayout_->addRow(tr("Name"), name_);
    connect(name_, &QLineEdit::editingFinished, this, &FeaturePanel::emitEdited);

    op_ = new QComboBox(form_);
    op_->addItems({tr("Add"), tr("Subtract (hole)"), tr("Intersect")});
    formLayout_->addRow(tr("Operation"), op_);
    connect(op_, &QComboBox::currentIndexChanged, this, &FeaturePanel::emitEdited);

    const double kMax = td::kMaxDimension;
    addField(tr("Width (X)"), &Feature::width, 0.01, kMax, kBox | kWedge);
    addField(tr("Depth (Y)"), &Feature::depth, 0.01, kMax, kBox | kWedge);
    addField(tr("Height (Z)"), &Feature::height, 0.01, kMax, kBox | kWedge | kCylinder | kStar | kGear | kCoil | kCapsule | kCurve);
    addField(tr("Edge rounding"), &Feature::corner, 0, kMax, kBox | kCylinder);
    addField(tr("Diameter"), &Feature::diameter, 0.01, kMax, kCylinder | kSphere | kTorus | kStar | kGear | kCoil | kCapsule | kCurve);
    addField(tr("Top diameter"), &Feature::topDiameter, -1, kMax, kCylinder | kCurve);
    addField(tr("Bend (degrees)"), &Feature::bend, -330, 330, kCurve);
    fields_.back().spin->setSpecialValueText(tr("same"));
    addField(tr("Tube diameter"), &Feature::tubeDiameter, 0.01, kMax, kTorus | kCoil);
    addField(tr("Turns"), &Feature::turns, 0.25, 200, kCoil);
    addField(tr("Inner diameter"), &Feature::innerDiameter, 0, kMax, kStar | kGear);

    sides_ = new QSpinBox(form_);
    sides_->setRange(0, td::kMaxGearTeeth);
    sides_->setSpecialValueText(tr("round"));
    sides_->setKeyboardTracking(false);
    formLayout_->addRow(tr("Sides"), sides_);
    connect(sides_, &QSpinBox::valueChanged, this, &FeaturePanel::emitEdited);

    const double kFar = 100000;
    addField(tr("X"), &Feature::x, -kFar, kFar, kAll);
    addField(tr("Y"), &Feature::y, -kFar, kFar, kAll);
    addField(tr("Z"), &Feature::z, -kFar, kFar, kAll);
    addField(tr("Rotate X"), &Feature::rx, -360, 360, kAll);
    addField(tr("Rotate Y"), &Feature::ry, -360, 360, kAll);
    addField(tr("Rotate Z"), &Feature::rz, -360, 360, kAll);

    loadSelected();
}

void FeaturePanel::addField(const QString& label, double Feature::*member, double min, double max, unsigned shapes) {
    auto* spin = new QDoubleSpinBox(form_);
    spin->setRange(min, max);
    spin->setDecimals(2);
    spin->setKeyboardTracking(false);
    formLayout_->addRow(label, spin);
    connect(spin, &QDoubleSpinBox::valueChanged, this, &FeaturePanel::emitEdited);
    fields_.push_back({member, shapes, spin});
}

std::vector<int> FeaturePanel::selectedRows() const {
    std::vector<int> rows;
    for (const QListWidgetItem* item : list_->selectedItems()) rows.push_back(list_->row(item));
    std::sort(rows.begin(), rows.end());
    return rows;
}

int FeaturePanel::selected() const {
    const auto rows = selectedRows();
    return rows.size() == 1 ? rows.front() : -1;
}

void FeaturePanel::applyRowSelection(const std::vector<int>& rows) {
    list_->clearSelection();
    for (int row : rows)
        if (row >= 0 && row < list_->count()) list_->item(row)->setSelected(true);
    if (rows.size() == 1) list_->setCurrentRow(rows.front(), QItemSelectionModel::NoUpdate);
}

void FeaturePanel::setRows(const std::vector<Row>& bodies, const std::vector<int>& rows) {
    loading_ = true;
    rows_ = bodies;
    list_->clear();
    for (const Row& row : rows_) list_->addItem(listLabel(row));
    applyRowSelection(rows);
    loading_ = false;
    loadSelected();
}

void FeaturePanel::setSelectedRows(const std::vector<int>& rows) {
    loading_ = true;
    applyRowSelection(rows);
    loading_ = false;
    loadSelected();
    emit selectionChanged();
}

void FeaturePanel::loadSelected() {
    const int row = selected();
    const bool valid = row >= 0 && row < int(rows_.size());
    form_->setVisible(valid);
    upButton_->setEnabled(valid && row > 0);
    downButton_->setEnabled(valid && row + 1 < int(rows_.size()));
    removeButton_->setEnabled(!selectedRows().empty());
    if (!valid) return;

    const bool wasLoading = loading_;
    loading_ = true;
    const Feature& f = rows_[row].feature;
    name_->setText(f.name);
    op_->setCurrentIndex(static_cast<int>(f.op));
    sides_->setValue(f.sides);
    for (const Field& field : fields_) field.spin->setValue(f.*field.member);
    loading_ = wasLoading;
    updateVisibleRows();
}

void FeaturePanel::updateVisibleRows() {
    const Row& row = rows_[selected()];
    const unsigned mask = row.editable ? maskOf(row.feature.shape) : 0;
    formLayout_->setRowVisible(op_, false);  // a body is a solid; cutting is done with Combine
    for (const Field& field : fields_) formLayout_->setRowVisible(field.spin, (field.shapes & mask) != 0);
    formLayout_->setRowVisible(sides_, ((kCylinder | kStar | kGear) & mask) != 0);
    formLayout_->labelForField(sides_)->setProperty("text", (kStar & mask) ? tr("Points") : (kGear & mask) ? tr("Teeth") : tr("Sides"));
    for (const Field& field : fields_)
        if (field.member == &Feature::innerDiameter) formLayout_->labelForField(field.spin)->setProperty("text", (kGear & mask) ? tr("Bore") : tr("Inner diameter"));
        else if (field.member == &Feature::tubeDiameter) formLayout_->labelForField(field.spin)->setProperty("text", (kCoil & mask) ? tr("Wire diameter") : tr("Tube diameter"));
}

void FeaturePanel::emitEdited() {
    const int row = selected();
    if (loading_ || row < 0 || row >= int(rows_.size())) return;
    Feature f = rows_[row].feature;
    f.name = name_->text();
    f.op = static_cast<Op>(op_->currentIndex());
    f.sides = sides_->value();
    for (const Field& field : fields_) f.*field.member = field.spin->value();

    // Tabbing through the name box fires this without any change.
    if (td::featureToJson(f) == td::featureToJson(rows_[row].feature)) return;
    rows_[row].feature = f;
    list_->item(row)->setText(listLabel(rows_[row]));
    emit featureEdited(row, f);
}
