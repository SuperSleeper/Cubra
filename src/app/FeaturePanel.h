#pragma once

#include "core/Feature.h"

#include <QWidget>

#include <vector>

class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class QWidget;

// Lists the bodies and edits the selected one. A body that is still a single
// primitive shows its dimensions; any body can be renamed.
class FeaturePanel : public QWidget {
    Q_OBJECT
public:
    struct Row {
        td::Feature feature;  // its name is the body's name
        bool editable;        // whether the dimensions can be changed here
        int depth;            // how many groups the body is inside
    };

    explicit FeaturePanel(QWidget* parent = nullptr);

    void setRows(const std::vector<Row>& rows, const std::vector<int>& selectedRows);
    void setSelectedRows(const std::vector<int>& rows);
    std::vector<int> selectedRows() const;  // ascending
    int selected() const;                   // the row when exactly one is selected, else -1

signals:
    void removeRequested(const std::vector<int>& rows);
    void moveRequested(int index, int delta);
    void selectionChanged();
    void featureEdited(int index, const td::Feature& feature);

private:
    struct Field {
        double td::Feature::*member;
        unsigned shapes;
        QDoubleSpinBox* spin;
    };

    void addField(const QString& label, double td::Feature::*member, double min, double max, unsigned shapes);
    void applyRowSelection(const std::vector<int>& rows);
    void loadSelected();
    void updateVisibleRows();
    void emitEdited();

    std::vector<Row> rows_;
    bool loading_ = false;

    QListWidget* list_;
    QPushButton* removeButton_;
    QPushButton* upButton_;
    QPushButton* downButton_;
    QWidget* form_;
    QFormLayout* formLayout_;
    QLineEdit* name_;
    QComboBox* op_;
    QSpinBox* sides_;
    std::vector<Field> fields_;
};
