#include "../include/CommandDialog.hpp"
#include <QIcon>
#include <QApplication>
#include <QClipboard>

namespace inferno {

CommandDialog::CommandDialog(const QString& agentIp, QWidget* parent)
    : QDialog(parent), m_agentIp(agentIp) {
    
    setWindowTitle("DISPATCH COMMAND | " + agentIp);
    setMinimumSize(500, 300);
    setStyleSheet("background-color: #0a0a0a; color: #00ff41;");

    auto* mainLayout = new QVBoxLayout(this);
    
    auto* header = new QLabel("REMOTE SHELL EXECUTION");
    header->setStyleSheet("font-family: 'Impact'; font-size: 18px; color: #00ff41;");
    mainLayout->addWidget(header);

    m_commandInput = new QPlainTextEdit();
    m_commandInput->setPlaceholderText("Enter multi-line command here...");
    m_commandInput->setStyleSheet(
        "QPlainTextEdit { "
        "  background-color: #000000; "
        "  color: #00ff41; "
        "  border: 2px solid #1a1a1a; "
        "  font-family: 'Courier New'; "
        "  font-size: 14px; "
        "  selection-background-color: #00ff41; "
        "  selection-color: #000000; "
        "}"
    );
    mainLayout->addWidget(m_commandInput);

    auto* btnLayout = new QHBoxLayout();
    
    auto* btnCopy = new QPushButton();
    btnCopy->setIcon(QIcon(":/icon_copy.png"));
    btnCopy->setToolTip("Copy");
    btnCopy->setFixedSize(40, 40);
    btnCopy->setObjectName("iconButton");
    connect(btnCopy, &QPushButton::clicked, this, &CommandDialog::onCopy);

    auto* btnPaste = new QPushButton();
    btnPaste->setIcon(QIcon(":/icon_paste.png"));
    btnPaste->setToolTip("Paste");
    btnPaste->setFixedSize(40, 40);
    btnPaste->setObjectName("iconButton");
    connect(btnPaste, &QPushButton::clicked, this, &CommandDialog::onPaste);

    auto* btnClear = new QPushButton();
    btnClear->setIcon(QIcon(":/icon_clear.png"));
    btnClear->setToolTip("Clear");
    btnClear->setFixedSize(40, 40);
    btnClear->setObjectName("iconButton");
    connect(btnClear, &QPushButton::clicked, this, &CommandDialog::onClear);

    auto* btnExecute = new QPushButton("EXECUTE");
    btnExecute->setFixedHeight(40);
    btnExecute->setStyleSheet(
        "QPushButton { "
        "  background-color: #0d0d0d; "
        "  color: #00ff41; "
        "  border: 1px solid #00ff41; "
        "  font-weight: bold; "
        "  padding: 0 20px; "
        "} "
        "QPushButton:hover { background-color: #00ff41; color: #000000; }"
    );
    connect(btnExecute, &QPushButton::clicked, this, &QDialog::accept);

    btnLayout->addWidget(btnCopy);
    btnLayout->addWidget(btnPaste);
    btnLayout->addWidget(btnClear);
    btnLayout->addStretch();
    btnLayout->addWidget(btnExecute);
    
    mainLayout->addLayout(btnLayout);
}

QString CommandDialog::getCommand() const {
    return m_commandInput->toPlainText();
}

void CommandDialog::onCopy() {
    m_commandInput->copy();
}

void CommandDialog::onPaste() {
    m_commandInput->paste();
}

void CommandDialog::onClear() {
    m_commandInput->clear();
}

} // namespace inferno
