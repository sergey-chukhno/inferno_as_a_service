#pragma once

#include <QDialog>
#include <QVBoxLayout>
#include <QLabel>
#include <QDateTime>
#include "Inferno_Database.hpp"

namespace inferno {

class AgentCardDialog : public QDialog {
    Q_OBJECT
public:
    explicit AgentCardDialog(const AgentProfile& profile, QWidget* parent = nullptr);
    virtual ~AgentCardDialog() = default;

private:
    void setupUI(const AgentProfile& profile);
};

} // namespace inferno
