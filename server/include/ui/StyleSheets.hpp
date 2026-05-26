#pragma once

#include <QString>

namespace inferno {
namespace ui {
namespace style {

const QString AGENT_LIST = 
    "QListWidget { background: #000; border: none; font-size: 14px; color: #00ff41; outline: none; }"
    "QListWidget::item { padding: 12px; margin: 2px; border-bottom: 1px solid #111; }"
    "QListWidget::item:selected { background: #0a0a0a; border: 2px solid #00ff41; color: #fff; border-radius: 4px; }"
    "QListWidget::item:hover { background: #111; }"
    "QScrollBar:vertical { background: #000; width: 10px; margin: 0px; }"
    "QScrollBar::handle:vertical { background: #1a1a1a; min-height: 20px; border-radius: 5px; }"
    "QScrollBar::handle:vertical:hover { background: #00ff41; }"
    "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }";

const QString TAB_WIDGET = 
    "QTabWidget::pane { border: 1px solid #1a1a1a; background: #000; }"
    "QTabBar::tab { background: #0c0c0c; color: #666; border: 1px solid #1a1a1a; padding: 10px 20px; font-weight: bold; font-size: 13px; }"
    "QTabBar::tab:selected { background: #000; color: #00ff41; border-bottom: 2px solid #00ff41; }"
    "QTabBar::tab:hover { background: #111; color: #fff; }";

const QString INPUT_BOX = 
    "background: #000; border: 1px solid #1a1a1a; color: #00ff41; padding: 5px;";

const QString COMBO_BOX = 
    "background: #000; color: #00ff41; border: 1px solid #1a1a1a; padding: 5px;";

const QString INTEL_TABLE = 
    "QTableWidget { background: #000; border: 1px solid #1a1a1a; color: #00ff41; gridline-color: #111; font-size: 13px; }"
    "QTableWidget::item { padding: 8px; }"
    "QTableWidget::item:selected { background: #00ff41; color: #000; }"
    "QHeaderView::section { background: #0c0c0c; color: #888; border: 1px solid #1a1a1a; padding: 6px; font-weight: bold; }"
    "QTableCornerButton::section { background: #0c0c0c; border: 1px solid #1a1a1a; }";

} // namespace style
} // namespace ui
} // namespace inferno
