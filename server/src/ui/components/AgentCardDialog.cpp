#include "../../../include/ui/components/AgentCardDialog.hpp"
#include <QFrame>
#include <QIcon>
#include <QPushButton>
#include <QGridLayout>

namespace inferno {

AgentCardDialog::AgentCardDialog(const AgentProfile& profile, QWidget* parent) : QDialog(parent) {
    setWindowTitle("Intelligence Card: " + profile.hostname);
    setFixedSize(450, 350);
    setStyleSheet("background-color: #0a0a0a; color: #00ff41; font-family: 'Consolas', 'Courier New', monospace;");
    
    setupUI(profile);
}

void AgentCardDialog::setupUI(const AgentProfile& profile) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(15);

    // Header: UUID
    auto* header = new QLabel("PERSISTENT IDENTITY");
    header->setStyleSheet("color: #666; font-size: 10px; font-weight: bold; letter-spacing: 2px;");
    layout->addWidget(header);

    auto* uuidLabel = new QLabel(profile.uuid);
    uuidLabel->setStyleSheet("font-size: 16px; font-weight: bold; color: #fff; background: #1a1a1a; padding: 10px; border-radius: 5px; border: 1px solid #333;");
    uuidLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(uuidLabel);

    // Details Grid
    auto* grid = new QGridLayout();
    grid->setSpacing(10);

    auto addField = [&](int row, const QString& label, const QString& value) {
        auto* l = new QLabel(label + ":");
        l->setStyleSheet("color: #00ff41; font-weight: bold;");
        auto* v = new QLabel(value);
        v->setStyleSheet("color: #ccc;");
        grid->addWidget(l, row, 0);
        grid->addWidget(v, row, 1);
    };

    addField(0, "HOSTNAME", profile.hostname);
    addField(1, "IP ADDRESS", profile.ip);
    addField(2, "OS PROFILE", profile.osInfo);
    addField(3, "STATUS", profile.isOnline ? "🟢 ONLINE" : "🔴 OFFLINE");
    
    layout->addLayout(grid);

    auto* sep = new QFrame();
    sep->setFrameShape(QFrame::HLine);
    sep->setFrameShadow(QFrame::Sunken);
    sep->setStyleSheet("background-color: #333;");
    layout->addWidget(sep);

    // Forensic Timestamps
    auto* timeLayout = new QVBoxLayout();
    
    auto* firstLabel = new QLabel("FIRST SEEN: " + profile.firstSeen.toString("yyyy-MM-dd HH:mm:ss"));
    firstLabel->setStyleSheet("color: #888; font-size: 11px;");
    timeLayout->addWidget(firstLabel);

    auto* lastLabel = new QLabel("LAST SEEN:  " + profile.lastSeen.toString("yyyy-MM-dd HH:mm:ss"));
    lastLabel->setStyleSheet("color: #888; font-size: 11px;");
    timeLayout->addWidget(lastLabel);

    layout->addLayout(timeLayout);
    layout->addStretch();

    auto* btnClose = new QPushButton("CLOSE PROFILE");
    btnClose->setStyleSheet("background: #1a1a1a; border: 1px solid #333; color: #fff; padding: 8px; font-weight: bold;");
    connect(btnClose, &QPushButton::clicked, this, &QDialog::accept);
    layout->addWidget(btnClose);
}

} // namespace inferno
