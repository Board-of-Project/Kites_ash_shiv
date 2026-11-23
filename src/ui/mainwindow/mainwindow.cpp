#include "mainwindow.h"
#include "common/reorder_program.h"
#include "ui/reorder_tab/reordertab.h"   // if not already added
#include <cstdlib>
#include <numeric>
#include "assembler/assembler.h"
#include "common/assembled_program.h"
#include "common/globals.h"
#include "common/instructions.h" // instruction_set::disassemble
#include "common/reorder_program.h" // same as above
#include "custom_pseudo_manager/custom_pseudo_manager.h"
#include "processor/processor_manager.h"
#include "ui/cache_tab/cachetab.h"
#include "ui/compiler_tab/compilertab.h"
#include "ui/dialogs/processor_dialog.h"
#include "ui/dialogs/settings_dialog.h"
#include "ui/editor_tab/editortab.h"
#include "ui/memory_tab/memorytab.h"
#include "ui/processor_tab/processortab.h"
#include "ui/profiler_tab/profilertab.h"
#include "ui/reorder_tab/reordertab.h" // added for ui for redored code
#include "ui/theme/icon_manager.h"
#include "ui_mainwindow.h"
#include "utils/utils.h"
#include <QActionGroup>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QSpinBox>
#include <QSplitter>
#include <QWidgetAction>

namespace Kites
{
MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
// ui(new Ui::MainWindow)
{
    // ui->setuoUi(this);
    setWindowTitle("Kites RISC-V Simulator");
    setWindowIcon(QIcon(":/icons/kite.png"));
    ThemeManager::getInstance(); // applies the persisted (or default) theme before any UI is built
    setupVmStateDirectory();

    // well run the vm in a separate thread to keep the ui responsive
    // do not touch these 4 line unless you really know what you are doing
    m_processorManager = new ProcessorManager();
    m_processorThread = new QThread(this);
    m_processorManager->moveToThread(m_processorThread);
    m_processorThread->start();

    m_registerContainer = new RegisterContainer(this, m_processorManager->getRegisterFile());
    QWidget *central = new QWidget(this);
    QHBoxLayout *mainLayout = new QHBoxLayout(central);
    QSplitter *splitter = new QSplitter(Qt::Horizontal, this);

    m_stackedTabs = new QStackedWidget(this);
    m_sidebar = new QListWidget(this);
    setUpStatusBar();
    setUpSidebar();
    setUpToolBar();
    setUpMenubar();
    setUpTabs();

    // m_registerContainer = new RegisterContainer(this);
    mainLayout->setSpacing(0);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    splitter->addWidget(m_stackedTabs);
    splitter->addWidget(m_registerContainer);
    splitter->widget(1)->setMaximumWidth(350);

    // outer splitter makes the sidebar width user-adjustable (Zed-style)
    QSplitter *outerSplitter = new QSplitter(Qt::Horizontal, this);
    outerSplitter->addWidget(m_sidebar);
    outerSplitter->addWidget(splitter);
    outerSplitter->setCollapsible(1, false);
    outerSplitter->setStretchFactor(0, 0);
    outerSplitter->setStretchFactor(1, 1);
    outerSplitter->setSizes({m_sidebar->sizeHintForColumn(0) + 24, 1000});

    mainLayout->addWidget(outerSplitter);

    setCentralWidget(central);
    resize(1200, 800);
    connect(this, &MainWindow::runProcessorSignal, m_processorManager, &ProcessorManager::runSlot);
    connect(this, &MainWindow::debugRunProcessorSignal, m_processorManager,
            &ProcessorManager::debugRunSlot);
    // well temporarily disable the toolbar buttons when vm is running
    connect(m_processorManager, &ProcessorManager::runFinishedSignal, this,
            &MainWindow::runFinishedSlot);

    connect(m_processorManager, &ProcessorManager::processorClockedSignal, this,
            [this](const ProcessorState &s) {
                const uint64_t idx = s.lastExecutedPC / 4;
                if (idx < m_execCounts.size())
                    ++m_execCounts[idx];
            });
    connect(m_processorManager, &ProcessorManager::runErrorSignal, this,
            [this](const QString &errorMessage, int sourceLine)
            {
                // first we enable the toolbar buttons
                runFinishedSlot();
                auto editorTab = dynamic_cast<EditorTab *>(m_tabs[TabIndex::EditorTabIndex]);
                if (editorTab)
                {
                    editorTab->showRuntimeError(sourceLine, errorMessage);
                }
                QMessageBox::critical(this, "Runtime Error", errorMessage);
            });

    // TODO : instead of using lambda just connect the signal to the functions directly
    connect(m_processorManager, &ProcessorManager::updateDisassemblySignal, this,
            [this](const QString &disassemblyText)
            {
                auto editorTab = dynamic_cast<EditorTab *>(m_tabs[TabIndex::EditorTabIndex]);
                if (editorTab)
                {
                    editorTab->updateDisassemblyView(disassemblyText.toStdString());
                }
            });
    connect(m_processorManager, &ProcessorManager::updateEditorHighlightSignal, this,
            [this](const std::vector<std::pair<int, std::string>> &editorLines,
                   const std::vector<std::pair<int, std::string>> &disassemblyLines)
            {
                auto editorTab = dynamic_cast<EditorTab *>(m_tabs[TabIndex::EditorTabIndex]);
                if (editorTab)
                {
                    editorTab->highlightLines(editorLines, disassemblyLines);
                }
            });
    connect(&ThemeManager::getInstance(), &ThemeManager::themeChangedSignal, this,
            &MainWindow::themeChangedSlot);
}

void MainWindow::setUpStatusBar()
{
    QStatusBar *statusBar = this->statusBar();
    QLabel *statusLabel = new QLabel("Ready", this);
    statusBar->addPermanentWidget(statusLabel);
}

void MainWindow::setUpToolBar()
{
    QToolBar *toolbar = addToolBar("Main Toolbar");
    QAction *processorAction = new QAction("Processor", this);
    QAction *runAction = new QAction("Run", this);
    QAction *pauseAction = new QAction("Pause", this);
    QAction *debugAction = new QAction("Debug Run", this);
    QAction *stepAction = new QAction("Step", this);
    QAction *undoAction = new QAction("Undo", this);
    QAction *redoAction = new QAction("Redo", this);

    stepAction->setDisabled(true);
    pauseAction->setDisabled(true);
    undoAction->setDisabled(true);
    redoAction->setDisabled(true);

    // set toolbar icons (resources in :/icons)
    runAction->setIcon(IconManager::getInstance().getIcon(Icon::Play));
    pauseAction->setIcon(IconManager::getInstance().getIcon(Icon::Pause));
    undoAction->setIcon(IconManager::getInstance().getIcon(Icon::Undo));
    redoAction->setIcon(IconManager::getInstance().getIcon(Icon::Redo));
    stepAction->setIcon(IconManager::getInstance().getIcon(Icon::Step));
    debugAction->setIcon(IconManager::getInstance().getIcon(Icon::Debug));
    // tooltips for accessibility
    processorAction->setToolTip("Processor settings");
    runAction->setToolTip("Run program");
    pauseAction->setToolTip("Pause execution");
    debugAction->setToolTip("Start debug run");
    stepAction->setToolTip("Execute single step");
    undoAction->setToolTip("Undo last VM step");
    redoAction->setToolTip("Redo last VM step");
    // optional: make icons a consistent size
    toolbar->setIconSize(QSize(18, 18));

    //------------Spinbox for setting execution speed----------------
    QSpinBox *spinbox = new QSpinBox(this);
    spinbox->setRange(1, 10000);
    spinbox->setValue(1000);
    spinbox->setSuffix(" ms");
    spinbox->setToolTip("Set Execution Speed (in milliseconds)");
    //---------------------------------------------------------------

    QWidgetAction *spinboxAction = new QWidgetAction(this);
    spinboxAction->setDefaultWidget(spinbox);
    toolbar->addAction(processorAction);
    toolbar->addAction(runAction);
    toolbar->addAction(debugAction);
    toolbar->addAction(spinboxAction);
    toolbar->addAction(pauseAction);
    toolbar->addAction(stepAction);
    toolbar->addSeparator();
    toolbar->addAction(undoAction);
    toolbar->addAction(redoAction);

    connect(runAction, &QAction::triggered, this,
            [this, processorAction, runAction, pauseAction, debugAction]()
            {
                if (runAction->text() == "Run")
                {
                    debugAction->setDisabled(true);
                    processorAction->setDisabled(true);
                    pauseAction->setEnabled(true);
                    runAction->setText("Stop");
                    runAction->setIcon(IconManager::getInstance().getIcon(Icon::Stop));
                    runAction->setToolTip("Stop execution");
                    this->run();
                }
                else if (runAction->text() == "Stop")
                {
                    // emit stopSignal();
                    runAction->setText("Run");
                    runAction->setIcon(IconManager::getInstance().getIcon(Icon::Play));
                    runAction->setToolTip("Run program");
                    m_processorManager->stop();
                }
            });

    connect(debugAction, &QAction::triggered, this,
            [this, processorAction, runAction, pauseAction, stepAction, undoAction, redoAction,
             debugAction]()
            {
                debugAction->setDisabled(true);
                processorAction->setDisabled(true);
                // debug run starts paused, so the toolbar starts in the paused state
                runAction->setText("Stop");
                runAction->setIcon(IconManager::getInstance().getIcon(Icon::Stop));
                runAction->setToolTip("Stop execution");
                pauseAction->setEnabled(true);
                pauseAction->setText("Resume");
                pauseAction->setIcon(IconManager::getInstance().getIcon(Icon::Resume));
                pauseAction->setToolTip("Resume execution");
                stepAction->setEnabled(true);
                undoAction->setEnabled(true);
                redoAction->setEnabled(true);
                this->run(true);
            });

    connect(processorAction, &QAction::triggered, this, &MainWindow::processorChangeDialog);

    connect(spinbox, qOverload<int>(&QSpinBox::valueChanged), this,
            [this](int value) { m_processorManager->setStepDelay(value); });

    connect(pauseAction, &QAction::triggered, this,
            [this, pauseAction, stepAction, undoAction, redoAction]()
            {
                if (pauseAction->text() == "Pause")
                {
                    pauseAction->setText("Resume");
                    pauseAction->setIcon(IconManager::getInstance().getIcon(Icon::Resume));
                    pauseAction->setToolTip("Resume execution");
                    stepAction->setEnabled(true);
                    undoAction->setEnabled(true);
                    redoAction->setEnabled(true);
                    m_processorManager->pause();
                }
                else
                {
                    pauseAction->setText("Pause");
                    pauseAction->setIcon(IconManager::getInstance().getIcon(Icon::Pause));
                    pauseAction->setToolTip("Pause execution");
                    stepAction->setDisabled(true);
                    undoAction->setDisabled(true);
                    redoAction->setDisabled(true);
                    m_processorManager->resume();
                }
            });

    // we also connect the vm paused at breakpoint signal to change the pause button text
    connect(m_processorManager, &ProcessorManager::processorPausedAtBreakpointSignal, this,
            [this, pauseAction, stepAction, undoAction, redoAction]()
            {
                pauseAction->setText("Resume");
                pauseAction->setIcon(IconManager::getInstance().getIcon(Icon::Resume));
                pauseAction->setToolTip("Resume execution");
                stepAction->setEnabled(true);
                undoAction->setEnabled(true);
                redoAction->setEnabled(true);
            });

    connect(stepAction, &QAction::triggered, this, [this]() { m_processorManager->step(); });
    connect(undoAction, &QAction::triggered, this, [this]() { m_processorManager->undo(); });
    connect(redoAction, &QAction::triggered, this, [this]() { m_processorManager->redo(); });
}

void MainWindow::setUpSidebar()
{
    auto &iconManager = IconManager::getInstance();
    m_sidebar->addItem(new QListWidgetItem(iconManager.getIcon(Icon::Editor), "Editor"));
    m_sidebar->addItem(new QListWidgetItem(iconManager.getIcon(Icon::Memory), "Memory"));
    m_sidebar->addItem(new QListWidgetItem(iconManager.getIcon(Icon::Processor), "Processor"));
    m_sidebar->addItem(new QListWidgetItem(iconManager.getIcon(Icon::Cache), "Cache"));
    m_sidebar->addItem(new QListWidgetItem(iconManager.getIcon(Icon::Compiler), "Compiler"));
    m_sidebar->addItem(new QListWidgetItem(iconManager.getIcon(Icon::Profiler), "Profiler"));
    m_sidebar->addItem(
        new QListWidgetItem(iconManager.getIcon(Icon::Compiler),
                            "Reorder")); // for ui of redoredered code this uses a exixting icon of
                                         // compiler no new icon is there
    m_sidebar->setIconSize(QSize(20, 20));
    m_sidebar->setMinimumWidth(48);
    m_sidebar->setCurrentRow(0);
    // m_sidebar->setFocusPolicy(Qt::ClickFocus);

    connect(m_sidebar, &QListWidget::currentRowChanged, m_stackedTabs,
            &QStackedWidget::setCurrentIndex);
}

void MainWindow::setUpMenubar()
{
    QMenu *fileMenu = menuBar()->addMenu("&File");
    QMenu *settingsMenu = menuBar()->addMenu("&Settings");
    QMenu *helpMenu = menuBar()->addMenu("&Help");

    QMenu *preferencesMenu = new QMenu("Preferences", this);
    QMenu *processorMenu = new QMenu("Processor", this);

    QAction *openAction = new QAction(IconManager::getInstance().getIcon(Icon::Open), "Open", this);
    QAction *saveAction = new QAction(IconManager::getInstance().getIcon(Icon::Save), "Save", this);
    QAction *exitAction = new QAction("Exit", this);
    QAction *aboutAction =
        new QAction(IconManager::getInstance().getIcon(Icon::About), "About", this);

    ///////////File Menu///////////////////
    fileMenu->addAction(openAction);
    fileMenu->addAction(saveAction);
    fileMenu->addSeparator();
    fileMenu->addAction(exitAction);
    ////////////Setting Menu////////////////
    settingsMenu->addMenu(preferencesMenu);
    QActionGroup *themeGroup = new QActionGroup(this);
    themeGroup->setExclusive(true);
    for (const ThemeData &theme : ThemeManager::getInstance().availableThemes())
    {
        QAction *themeAction = new QAction(theme.name, this);
        themeAction->setCheckable(true);
        themeAction->setChecked(theme.id == ThemeManager::getInstance().currentThemeId());
        themeGroup->addAction(themeAction);
        preferencesMenu->addAction(themeAction);
        connect(themeAction, &QAction::triggered, this,
                [this, id = theme.id]() { toggleTheme(id); });
    }
    //----------------------------------------------
    settingsMenu->addMenu(processorMenu);
    QAction *wireStayActiveAction = new QAction("Wires Stay Active", this);
    wireStayActiveAction->setCheckable(true);
    wireStayActiveAction->setChecked(false);
    processorMenu->addAction(wireStayActiveAction);
    connect(wireStayActiveAction, &QAction::toggled, this,
            [this](bool checked)
            {
                auto processorTab =
                    dynamic_cast<ProcessorTab *>(m_tabs[TabIndex::ProcessorTabIndex]);
                if (processorTab)
                {
                    processorTab->setWiresStayActive(checked);
                }
            });
    //--------------------------------------------------
    QAction *advancedSettingsAction =
        new QAction(IconManager::getInstance().getIcon(Icon::Settings), "Advanced Settings", this);
    connect(advancedSettingsAction, &QAction::triggered, this,
            [this]()
            {
                SettingsDialog dialog(this);
                dialog.setWindowTitle("Advanced Processor Settings");
                dialog.exec();
            });
    settingsMenu->addAction(advancedSettingsAction);
    ///////////Help Menu///////////////////
    helpMenu->addAction(aboutAction);

    connect(openAction, &QAction::triggered, this,
            [this]()
            {
                QString filename = QFileDialog::getOpenFileName(
                    this, "Open File", "", "Assembly Files (*.asm *.s);;All Files (*)");

                if (!filename.isEmpty())
                {
                    QFile file(filename);
                    if (file.open(QIODevice::ReadOnly | QIODevice::Text))
                    {
                        QTextStream in(&file);
                        QString content = in.readAll();
                        auto editorTab =
                            dynamic_cast<EditorTab *>(m_tabs[TabIndex::EditorTabIndex]);
                        if (editorTab)
                        {
                            editorTab->setRawText(content);
                        }
                        file.close();
                    }
                }
            });

    connect(saveAction, &QAction::triggered, this,
            [this]()
            {
                QString filename = QFileDialog::getSaveFileName(
                    this, "Save File", "", "Assembly Files (*.asm *.s);;All Files (*)");

                if (!filename.isEmpty())
                {
                    QFile file(filename);
                    if (file.open(QIODevice::WriteOnly | QIODevice::Text))
                    {
                        QTextStream out(&file);
                        auto editorTab =
                            dynamic_cast<EditorTab *>(m_tabs[TabIndex::EditorTabIndex]);
                        if (editorTab)
                        {
                            out << editorTab->getRawText().c_str();
                        }
                        file.close();
                    }
                }
            });
}

void MainWindow::setUpTabs()
{
    m_tabs[TabIndex::EditorTabIndex] = new EditorTab(this);
    m_tabs[TabIndex::MemoryTabIndex] =
        new MemoryTab(this, m_processorManager->getMemoryController());
    m_tabs[TabIndex::ProcessorTabIndex] = new ProcessorTab(this, m_processorManager);
    m_tabs[TabIndex::CacheTabIndex] = new CacheTab(this, m_processorManager->getMemoryController());
    m_tabs[TabIndex::CompilerTabIndex] = new CompilerTab(this);
    m_tabs[TabIndex::ProfilerTabIndex] = new ProfilerTab(this, m_processorManager->getProfiler());
    m_tabs[TabIndex::ReorderTabIndex] = new ReorderTab(this); // tab for reordered code in ui
    // a little experiment
    connect(this, &MainWindow::processorChangedSignal,
            dynamic_cast<ProcessorTab *>(m_tabs[TabIndex::ProcessorTabIndex]),
            &ProcessorTab::onVMChanged);

    m_stackedTabs->addWidget(m_tabs[TabIndex::EditorTabIndex]);
    m_stackedTabs->addWidget(m_tabs[TabIndex::MemoryTabIndex]);
    m_stackedTabs->addWidget(m_tabs[TabIndex::ProcessorTabIndex]);
    m_stackedTabs->addWidget(m_tabs[TabIndex::CacheTabIndex]);
    m_stackedTabs->addWidget(m_tabs[TabIndex::CompilerTabIndex]);
    m_stackedTabs->addWidget(m_tabs[TabIndex::ProfilerTabIndex]);
    m_stackedTabs->addWidget(m_tabs[TabIndex::ReorderTabIndex]); // ui tab for reoredred code
    connect(static_cast<ReorderTab *>(m_tabs[TabIndex::ReorderTabIndex]),
            &ReorderTab::exportRequested, this, &MainWindow::exportReorderTestCase);
}

ReorderResult MainWindow::showReorderModel(const std::vector<uint32_t> &words, ProcessorType t,
                                           const std::vector<long long> &counts)
{
    ReorderResult r = reorderWords(words, t == ProcessorType::RV5Stage_H_F, counts);
    const QByteArray raw(reinterpret_cast<const char *>(words.data()),
                         static_cast<int>(words.size() * 4));
    const QString key = QString::number(static_cast<int>(t)) + ":" + QString::number(qHash(raw));
    if (auto *tab = dynamic_cast<ReorderTab *>(m_tabs[TabIndex::ReorderTabIndex]))
    {
        std::vector<ReorderedLine> lines;
        lines.reserve(r.words.size());
        for (size_t k = 0; k < r.words.size(); ++k)
        {
            ReorderedLine line;
            line.from = k < r.origIndex.size() ? r.origIndex[k] : static_cast<int>(k);
            line.address = static_cast<quint64>(k) * 4;
            line.word = r.words[k];
            line.text = QString::fromStdString(instruction_set::disassemble(r.words[k]));
            lines.push_back(line);
        }

        // Original program for the left pane: row k is original instruction k, and "from"
        // holds the slot it moved to (inverse of origIndex).
        std::vector<ReorderedLine> original(words.size());
        for (size_t k = 0; k < words.size(); ++k)
        {
            original[k].from = static_cast<int>(k); // unmoved unless overwritten below
            original[k].address = static_cast<quint64>(k) * 4;
            original[k].word = words[k];
            original[k].text = QString::fromStdString(instruction_set::disassemble(words[k]));
        }
        for (size_t slot = 0; slot < r.origIndex.size(); ++slot)
        {
            const int o = r.origIndex[slot];
            if (o >= 0 && static_cast<size_t>(o) < original.size())
                original[static_cast<size_t>(o)].from = static_cast<int>(slot);
        }
        tab->setReordered(lines, key, r.cyclesBefore, r.cyclesAfter, r.stallsBefore,
                          r.stallsAfter, r.weighted, original);
    }
    return r;
}

bool MainWindow::tryParseAndLoadProgram()
{
    auto *editor = dynamic_cast<EditorTab *>(m_tabs[TabIndex::EditorTabIndex]);
    auto *profilerTab = dynamic_cast<ProfilerTab *>(m_tabs[TabIndex::ProfilerTabIndex]);

    if (!editor || !profilerTab)
    {
        QMessageBox::critical(this, "Error", "Editor or Profiler tab not found.");
        return false;
    }
    editor->switchToExpandedView(); // switch to the expanded view
                                    // to show expanded pseudoinstructions
    editor->resetErrorLines();      // we reset previous error lines
    editor->setCanWrite(false);     // we disable writing in editor while vm is running
    m_processorManager->reset();
    try
    {
        std::string rawText = editor->getRawText();
        rawText = customPseudoManager::expandPseudoInstruction(rawText);
        std::ofstream out(globals::temporary_assembly_file_path);
        out << rawText;
        out.close();
        AssembledProgram assembledProgram =
            assemble(globals::temporary_assembly_file_path.string());


        m_loadedReordered = false;
        const std::vector<uint32_t> originalWords = assembledProgram.text_buffer;
        const std::vector<long long> weights =
            (m_countsUsable && m_countsProgram == originalWords) ? m_execCounts
                                                                 : std::vector<long long>{};
        if (auto *reorderTab = dynamic_cast<ReorderTab *>(m_tabs[TabIndex::ReorderTabIndex]))
        {
            const ProcessorType t = m_processorManager->getProcessorType();
            if (t == ProcessorType::RV5Stage_H_NF || t == ProcessorType::RV5Stage_H_F)
            {
                try
                {
                    ReorderResult r = showReorderModel(originalWords, t, weights);
                    if (reorderTab->executeReordered())
                    {
                        assembledProgram = applyReordering(assembledProgram, r);
                        m_loadedReordered = true;
                    }
                }
                catch (const std::exception &)
                {
                    reorderTab->clear();
                    m_loadedReordered = false;
                }
            }
            else
            {
                reorderTab->clear();
            }
        }

        m_countsProgram = originalWords;                  // start counting for this run
        m_execCounts.assign(originalWords.size(), 0);
        m_countsUsable = false;


        DumpDisasssembly(globals::disassembly_file_path, assembledProgram);
        std::ifstream in(globals::disassembly_file_path);
        std::stringstream buffer;
        buffer << in.rdbuf();
        editor->updateDisassemblyView(buffer.str());
        profilerTab->setSourceText(QString::fromStdString(rawText));

        // if (auto *reorderTab = dynamic_cast<ReorderTab *>(m_tabs[TabIndex::ReorderTabIndex]))
        // {
        //     const ProcessorType t = m_processorManager->getProcessorType();
        //     if (t == ProcessorType::RV5Stage_H_NF || t == ProcessorType::RV5Stage_H_F)
        //     {
        //         try
        //         {
        //             ReorderResult r = reorderWords(assembledProgram.text_buffer,
        //                                            t == ProcessorType::RV5Stage_H_F);
        //             reorderTab->setReordered(QString::fromStdString(formatReordered(r)),
        //                                      r.cyclesBefore, r.cyclesAfter);
        //         }
        //         catch (const std::exception &)
        //         {
        //             reorderTab->clear();   // a reorderer failure must not block loading the
        //             program
        //         }
        //     }
        //     else
        //     {
        //         reorderTab->clear();
        //     }
        // }

        m_processorManager->loadProgram(assembledProgram);
        m_processorManager->setBreakpoints(editor->getBreakpoints());
        return true;
    }
    catch (const std::exception &e)
    {
        editor->setErrorLinesFromFile(globals::errors_dump_file_path);
        QMessageBox::critical(this, "Error", e.what());
        return false;
    }
}

void MainWindow::run(bool startPaused)
{
    // as soon s as run is clicked we disable the toolbar buttons
    // disableToolBarButtons();
    if (tryParseAndLoadProgram())
    {

        auto editorTab = dynamic_cast<EditorTab *>(m_tabs[TabIndex::EditorTabIndex]);
        if (editorTab)
        {
            editorTab->setExpandedLocked(true);
        }

        if (startPaused)
        {
            emit debugRunProcessorSignal();
        }
        else
        {
            emit runProcessorSignal();
        }
    }
    else
    {
        // if parsing or loading failed we re-enable the buttons
        // otherwise if the vm starts running the buttons will be re-enabled when vm finishes
        runFinishedSlot();
    }
}
void MainWindow::processorChangeDialog()
{
    ProcessorDialog dialog(this, m_processorManager->getProcessorType());
    dialog.setWindowTitle("Choose Processor");
    connect(&dialog, &ProcessorDialog::vmSelected, this, &MainWindow::processorChanged);
    dialog.exec();
}

void MainWindow::processorChanged(const ProcessorType &vmType)
{
    m_processorManager->changeProcessor(vmType);
    m_registerContainer->setRegisterFile(m_processorManager->getRegisterFile());
    auto *memtab = dynamic_cast<MemoryTab *>(m_tabs[TabIndex::MemoryTabIndex]);
    if (memtab)
    {
        memtab->changeMemoryController(m_processorManager->getMemoryController());
    }
    auto *cacheTab = dynamic_cast<CacheTab *>(m_tabs[TabIndex::CacheTabIndex]);
    if (cacheTab)
    {
        cacheTab->changeMemoryController(m_processorManager->getMemoryController());
    }
    emit processorChangedSignal();

    // well also have to change the processor design from here later
}

void MainWindow::runFinishedSlot()
{
    auto *editor = dynamic_cast<EditorTab *>(m_tabs[TabIndex::EditorTabIndex]);
    editor->setCanWrite(true); // re-enable writing in editor when vm stops
    editor->setExpandedLocked(false);
    editor->clearHighlights(); // we clear any highlights when processor stops
    // other we are not alble to move the cursor as the paint
    //  keeps jumping to last highlighted line
    editor->switchToEditorView();
    QList<QToolBar *> toolbars = this->findChildren<QToolBar *>();
    // since we only have one toolbar we can directly access it
    for (QAction *action : toolbars[0]->actions())
    {
        if (action->text() == "Run" || action->text() == "Processor" ||
            action->text() == "Debug Run")
        {
            action->setEnabled(true);
        }
        if (action->text() == "Stop")
        {
            action->setText("Run");
            action->setIcon(IconManager::getInstance().getIcon(Icon::Play));
            action->setToolTip("Run program");
            // when the program finishes we set the run button text back to run
        }
        if (action->text() == "Resume")
        {
            action->setText("Pause");
            action->setIcon(IconManager::getInstance().getIcon(Icon::Pause));
            action->setToolTip("Pause execution");
            // in case we stop while paused
        }

        if (action->text() == "Pause" || action->text() == "Stop" || action->text() == "Step")
        {
            action->setDisabled(true);
        }
    }

    const long long total = std::accumulate(m_execCounts.begin(), m_execCounts.end(), 0LL);
    const long long retired = static_cast<long long>(m_processorManager->getInstructionsRetired());
    // an ecall stops the run while its older instructions are still in MEM/WB, so allow a small gap
    m_countsUsable = !m_execCounts.empty() && std::llabs(total - retired) <= 4;

    m_lastMeasured = MeasuredStats{static_cast<long long>(m_processorManager->getCycles()), retired,
                                   static_cast<long long>(m_processorManager->getTotalStallCycles())};
    m_lastRanReordered = m_loadedReordered;
    m_hasMeasured = true;

    const ProcessorType t = m_processorManager->getProcessorType();
    if (m_countsUsable &&
        (t == ProcessorType::RV5Stage_H_NF || t == ProcessorType::RV5Stage_H_F))
        showReorderModel(m_countsProgram, t, m_execCounts);   // model now uses the real counts

    if (auto *reorderTab = dynamic_cast<ReorderTab *>(m_tabs[TabIndex::ReorderTabIndex]))
        reorderTab->setMeasured(m_lastRanReordered, m_processorManager->getCycles(),
                                m_processorManager->getTotalStallCycles(), retired);
}

void MainWindow::exportReorderTestCase()
{
    if (m_countsProgram.empty())
    {
        QMessageBox::information(this, "Export", "Run a program first.");
        return;
    }
    const QString path = QFileDialog::getSaveFileName(this, "Export reorder test case",
                                                      "reorder_testcase.txt", "Text (*.txt)");
    if (path.isEmpty())
        return;
    TestCase tc;
    tc.words = m_countsProgram;
    tc.forwarding = (m_processorManager->getProcessorType() == ProcessorType::RV5Stage_H_F);
    if (m_countsUsable)
        tc.counts = m_execCounts;
    tc.hasMeasured = m_hasMeasured;
    tc.measured = m_lastMeasured;
    tc.ranReordered = m_lastRanReordered;
    if (!writeTestCase(path.toStdString(), tc))
        QMessageBox::warning(this, "Export", "Could not write the file.");
}

void MainWindow::toggleTheme(const QString &themeId)
{
    ThemeManager::getInstance().setTheme(themeId);
}

void MainWindow::themeChangedSlot([[maybe_unused]] const QString &themeId)
{
    // we will update the icons here based on the theme
    // this is very very hacky but this will do for now
    auto *toolbar = this->findChild<QToolBar *>();
    if (toolbar)
    {
        for (QAction *action : toolbar->actions())
        {
            if (action->text() == "Run")
            {
                action->setIcon(IconManager::getInstance().getIcon(Icon::Play));
            }
            else if (action->text() == "Pause")
            {
                action->setIcon(IconManager::getInstance().getIcon(Icon::Pause));
            }
            else if (action->text() == "Resume")
            {
                action->setIcon(IconManager::getInstance().getIcon(Icon::Resume));
            }
            else if (action->text() == "Stop")
            {
                action->setIcon(IconManager::getInstance().getIcon(Icon::Stop));
            }
            else if (action->text() == "Undo")
            {
                action->setIcon(IconManager::getInstance().getIcon(Icon::Undo));
            }
            else if (action->text() == "Redo")
            {
                action->setIcon(IconManager::getInstance().getIcon(Icon::Redo));
            }
            else if (action->text() == "Step")
            {
                action->setIcon(IconManager::getInstance().getIcon(Icon::Step));
            }
        }
    }

    // sidebar icons
    static const std::array<Icon, toIndex(TabIndex::TabCount)> sidebarIcons = {
        Icon::Editor,   Icon::Memory,   Icon::Processor, Icon::Cache,
        Icon::Compiler, Icon::Profiler, Icon::Compiler};
    for (int row = 0; row < m_sidebar->count(); ++row)
    {
        if (row < static_cast<int>(sidebarIcons.size()))
            m_sidebar->item(row)->setIcon(IconManager::getInstance().getIcon(sidebarIcons[row]));
    }

    // menu bar action icons
    for (QAction *action : this->findChildren<QAction *>())
    {
        if (action->text() == "Open")
            action->setIcon(IconManager::getInstance().getIcon(Icon::Open));
        else if (action->text() == "Save")
            action->setIcon(IconManager::getInstance().getIcon(Icon::Save));
        else if (action->text() == "About")
            action->setIcon(IconManager::getInstance().getIcon(Icon::About));
        else if (action->text() == "Advanced Settings")
            action->setIcon(IconManager::getInstance().getIcon(Icon::Settings));
    }

    
}

MainWindow::~MainWindow()
{
    m_processorManager->stop();
    if (m_processorThread->isRunning())
    {
        m_processorThread->quit();
        m_processorThread->wait();
    }
    // delete ui;
}
} // namespace Kites