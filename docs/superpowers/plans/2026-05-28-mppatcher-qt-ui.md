# MPPatcher Qt5 UI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build `MPPatcher.exe` — a standalone Qt5 C++ GUI application that generates BodySlide automation XML scripts and drives OutfitStudio headlessly to add `RM_Heavy`, `RM_Muscular`, `RM_Thin` sliders to outfit projects.

**Architecture:** Standalone Qt5 C++ application (NOT a fork of BodySlide source). Reads BodySlide SliderGroup XML files to populate the group picker, generates OS automation XML, writes to `<OS data>/Automations/`, spawns `OutfitStudio.exe -automation <name>` as a subprocess. The Conform All algorithm runs entirely inside OutfitStudio — MPPatcher generates instructions only.

**Tech Stack:** C++17, Qt5.15, CMake 3.20+, MSVC 2022, QuaZip (for backup zip), tinyxml2 (for XML writing). Project lives in `E:\SkyrimDev\MPPatcher\` (separate repo from BodySlide fork and RleyBodyMorphs).

**Gate — Task 2:** Proof of concept must pass before any UI code is written. If OS headless automation doesn't work as expected, the approach needs adjustment before investing in the UI.

---

## File Map

```
E:\SkyrimDev\MPPatcher\
├── CMakeLists.txt                  ← root build config
├── src/
│   ├── main.cpp                    ← QApplication entry, MainWindow launch
│   ├── MainWindow.h/.cpp           ← main window, wires all components together
│   ├── SliderGroupReader.h/.cpp    ← reads CalienteTools/BodySlide/SliderGroups/*.xml
│   ├── AutomationXmlWriter.h/.cpp  ← generates OS automation XML for a patch run
│   ├── OsRunner.h/.cpp             ← locates OutfitStudio.exe, spawns subprocess
│   ├── BackupManager.h/.cpp        ← zip backup / restore of .osp + .osd files
│   └── Config.h/.cpp               ← QSettings persistence (paths, last-used prefs)
└── tests/
    ├── CMakeLists.txt
    ├── test_slider_group_reader.cpp
    └── test_automation_xml_writer.cpp
```

---

## Task 1: Qt5 Project Setup

**Files:**
- Create: `E:\SkyrimDev\MPPatcher\CMakeLists.txt`
- Create: `E:\SkyrimDev\MPPatcher\src\main.cpp`

- [ ] **Step 1: Create `CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.20)
project(MPPatcher VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt5 REQUIRED COMPONENTS Core Gui Widgets Xml Process)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

add_executable(MPPatcher WIN32
    src/main.cpp
    src/MainWindow.h
    src/MainWindow.cpp
    src/SliderGroupReader.h
    src/SliderGroupReader.cpp
    src/AutomationXmlWriter.h
    src/AutomationXmlWriter.cpp
    src/OsRunner.h
    src/OsRunner.cpp
    src/BackupManager.h
    src/BackupManager.cpp
    src/Config.h
    src/Config.cpp
)

target_link_libraries(MPPatcher PRIVATE
    Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Xml Qt5::Network
)

target_include_directories(MPPatcher PRIVATE src)
```

- [ ] **Step 2: Create `src/main.cpp`**

```cpp
#include <QApplication>
#include "MainWindow.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("MPPatcher");
    app.setApplicationVersion("0.1");
    app.setOrganizationName("ModernPhysiquePicker");

    MainWindow w;
    w.show();
    return app.exec();
}
```

- [ ] **Step 3: Create stub `MainWindow.h/.cpp` that shows an empty window**

```cpp
// MainWindow.h
#pragma once
#include <QMainWindow>

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
};

// MainWindow.cpp
#include "MainWindow.h"
MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("MPPatcher — ModernPhysiquePicker Outfit Patcher");
    resize(600, 500);
}
```

Create empty stubs for all other `.h/.cpp` files (just empty class declarations).

- [ ] **Step 4: Build and run**

```powershell
cd E:\SkyrimDev\MPPatcher
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="C:\Qt\5.15.2\msvc2019_64"
cmake --build build --config Release
.\build\Release\MPPatcher.exe
```

Expected: empty window with title "MPPatcher — ModernPhysiquePicker Outfit Patcher" opens.

- [ ] **Step 5: Init git repo and commit**

```powershell
cd E:\SkyrimDev\MPPatcher
git init
git add .
git commit -m "feat: initial Qt5 project skeleton"
```

---

## Task 2: Proof of Concept — Manual Automation XML Test

**⛔ GATE: Complete this task and verify results before writing any further code.**

**Files:**
- Create: `E:\tmp\mppatcher_poc\mppatcher_test.xml`
- Read: OS Automations folder path from OutfitStudio settings

- [ ] **Step 1: Find the OutfitStudio Automations folder**

Run OutfitStudio once normally, open Settings and note the "Project Path". The Automations folder is `<ProjectPath>/Automations/`. If ProjectPath is empty, it falls back to the directory where `OutfitStudio.exe` lives.

Common MO2 location: `<game data>\CalienteTools\BodySlide\Automations\`

Create the folder if it doesn't exist.

- [ ] **Step 2: Create a minimal test automation XML**

Write this file as `<Automations folder>/mppatcher_poc.xml`:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<AutomationScript>
    <Step type="ClearReference" />
    <Step type="LoadReference">
        <SourceFile>CalienteTools/BodySlide/SliderSets/Rley COR - CBBE Body.osp</SourceFile>
        <Set>Rley COR - CBBE Body</Set>
        <MergeSliders>true</MergeSliders>
    </Step>
    <Step type="ConformSliders">
        <SliderNames>RM_Heavy,RM_Muscular,RM_Thin</SliderNames>
        <ProximityRadius>10.0</ProximityRadius>
        <MaxResults>10</MaxResults>
    </Step>
    <Step type="SaveProject" />
</AutomationScript>
```

- [ ] **Step 3: Copy one test outfit to a safe location**

```powershell
$src = "E:\Sculpting\00000 PC Ver 2\Packages\FOS 3BA Vanilla Outfits Refit\CalienteTools\BodySlide\SliderSets"
$dst = "E:\tmp\mppatcher_poc"
New-Item -ItemType Directory -Force $dst
Copy-Item "$src\FOS - Vanilla Outfits 3BA Refit.osp" $dst
# Also copy the associated ShapeData folder (contains the .osd files)
Copy-Item "$src\..\ShapeData\CBBE 3BA Vanilla (Im Physics)" "$dst\ShapeData\CBBE 3BA Vanilla (Im Physics)" -Recurse
```

Note: the .osp's `<DataFolder>` points inside ShapeData — copy whatever folder is referenced in the .osp.

- [ ] **Step 4: Point OutfitStudio's ProjectPath to the test folder**

Open OutfitStudio settings, temporarily set "Project Path" to `E:\tmp\mppatcher_poc`. This makes OS look for the .osp files there.

OR: pass the project via command line: `OutfitStudio.exe -proj "FOS - Vanilla Outfits 3BA Refit" -automation mppatcher_poc`

- [ ] **Step 5: Run OutfitStudio headlessly via MO2**

In MO2, configure OutfitStudio's executable arguments to include:
```
-automation mppatcher_poc
```

Launch from MO2. Expected: window never appears, process exits with code 0.

- [ ] **Step 6: Verify the output**

Open the modified `FOS - Vanilla Outfits 3BA Refit.osp` in OutfitStudio normally. Verify:
- `RM_Heavy`, `RM_Muscular`, `RM_Thin` sliders now exist on outfit shapes
- Original CBBE/3BA sliders are untouched
- Reference shape is the MPP CBBE body

- [ ] **Step 7: Document findings**

Add a section to `docs/superpowers/audit-bs-source.md`:
```
## POC Results

- Headless confirmed: yes/no
- Sliders added: RM_Heavy / RM_Muscular / RM_Thin — present: yes/no
- Original sliders preserved: yes/no
- Exact command used: OutfitStudio.exe [args]
- Issues encountered: [none / describe]
```

**⛔ Only proceed to Task 3 after Step 6 passes.**

---

## Task 3: SliderGroupReader

**Files:**
- Create: `src/SliderGroupReader.h`
- Create: `src/SliderGroupReader.cpp`
- Create: `tests/test_slider_group_reader.cpp`

- [ ] **Step 1: Create `src/SliderGroupReader.h`**

```cpp
#pragma once
#include <QString>
#include <QStringList>
#include <QMap>

struct SliderGroupInfo {
    QString name;
    QStringList memberNames;   // SliderSet names in this group
};

class SliderGroupReader {
public:
    // Read all SliderGroup XML files from the given folder
    // (CalienteTools/BodySlide/SliderGroups/ as seen via MO2 VFS)
    static QList<SliderGroupInfo> readAll(const QString& sliderGroupsFolder);

    // Read a single SliderGroup XML file
    static QList<SliderGroupInfo> readFile(const QString& filePath);
};
```

- [ ] **Step 2: Create `src/SliderGroupReader.cpp`**

```cpp
#include "SliderGroupReader.h"
#include <QDir>
#include <QFile>
#include <QXmlStreamReader>

QList<SliderGroupInfo> SliderGroupReader::readAll(const QString& folder) {
    QList<SliderGroupInfo> result;
    QDir dir(folder);
    auto files = dir.entryList({"*.xml"}, QDir::Files);
    for (const auto& f : files) {
        auto groups = readFile(dir.filePath(f));
        result.append(groups);
    }
    return result;
}

QList<SliderGroupInfo> SliderGroupReader::readFile(const QString& filePath) {
    QList<SliderGroupInfo> result;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return result;

    QXmlStreamReader xml(&file);
    SliderGroupInfo current;

    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            if (xml.name() == QLatin1String("Group")) {
                current = SliderGroupInfo{};
                current.name = xml.attributes().value("name").toString();
            } else if (xml.name() == QLatin1String("Member") && !current.name.isEmpty()) {
                current.memberNames << xml.attributes().value("name").toString();
            }
        } else if (xml.isEndElement() && xml.name() == QLatin1String("Group")) {
            if (!current.name.isEmpty())
                result.append(current);
            current = {};
        }
    }
    return result;
}
```

- [ ] **Step 3: Write failing unit test**

```cpp
// tests/test_slider_group_reader.cpp
#include <catch2/catch_test_macros.hpp>
#include "SliderGroupReader.h"
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>

// Write a minimal SliderGroup XML, parse it, verify
TEST_CASE("SliderGroupReader parses group name and members", "[sgr]") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    QString xmlContent = R"(<?xml version="1.0"?>
<SliderGroups>
    <Group name="CBBE Vanilla Outfits">
        <Member name="Rley 3BA Vanilla - Body - Ancient Nord"/>
        <Member name="Rley 3BA Vanilla - Body - Bandit"/>
    </Group>
    <Group name="HIMBO Vanilla Outfits">
        <Member name="Rley HIMBO Vanilla - Body - Guard"/>
    </Group>
</SliderGroups>)";

    QFile f(dir.filePath("test.xml"));
    f.open(QIODevice::WriteOnly);
    f.write(xmlContent.toUtf8());
    f.close();

    auto groups = SliderGroupReader::readFile(dir.filePath("test.xml"));
    REQUIRE(groups.size() == 2);
    REQUIRE(groups[0].name == "CBBE Vanilla Outfits");
    REQUIRE(groups[0].memberNames.size() == 2);
    REQUIRE(groups[0].memberNames[0] == "Rley 3BA Vanilla - Body - Ancient Nord");
    REQUIRE(groups[1].name == "HIMBO Vanilla Outfits");
    REQUIRE(groups[1].memberNames.size() == 1);
}
```

- [ ] **Step 4: Run test — verify it fails**

```powershell
cmake --build build --target mppatcher-tests
ctest --test-dir build -R test_slider_group_reader --output-on-failure
```

Expected: FAIL.

- [ ] **Step 5: Run test after implementation — verify it passes**

```powershell
cmake --build build --target mppatcher-tests
ctest --test-dir build -R test_slider_group_reader --output-on-failure
```

Expected: PASS.

- [ ] **Step 6: Commit**

```powershell
git add src/SliderGroupReader.h src/SliderGroupReader.cpp tests/
git commit -m "feat: add SliderGroupReader with unit tests"
```

---

## Task 4: AutomationXmlWriter

**Files:**
- Create: `src/AutomationXmlWriter.h`
- Create: `src/AutomationXmlWriter.cpp`
- Create: `tests/test_automation_xml_writer.cpp`

- [ ] **Step 1: Create `src/AutomationXmlWriter.h`**

```cpp
#pragma once
#include <QString>
#include <QStringList>

enum class MppGender { CBBE, HIMBO };
enum class MppMeshType { Body, Hands, Feet };

struct PatchRunConfig {
    QStringList sliderSetNames;     // SliderSets to process (from selected groups)
    QString sliderSetsFolder;       // path to CalienteTools/BodySlide/SliderSets
    QString outputFolder;           // empty = overwrite, non-empty = new project folder
    QString outputPrefix;           // "MPP_" for new project mode
    MppGender gender;
    MppMeshType meshType;
};

struct MppReferences {
    // Returns the OSP file name for a given gender+mesh type
    static QString ospName(MppGender gender, MppMeshType meshType);
    // Returns the SliderSet name inside that OSP
    static QString setName(MppGender gender, MppMeshType meshType);
};

class AutomationXmlWriter {
public:
    // Generate automation XML content for a patch run
    static QString generate(const PatchRunConfig& config);

    // Write the XML to disk and return the script name (without .xml)
    // scriptPath = <automationsFolder>/mppatcher_<timestamp>.xml
    static QString writeToFolder(const PatchRunConfig& config, const QString& automationsFolder);
};
```

- [ ] **Step 2: Create `src/AutomationXmlWriter.cpp`**

```cpp
#include "AutomationXmlWriter.h"
#include <QXmlStreamWriter>
#include <QFile>
#include <QDateTime>

QString MppReferences::ospName(MppGender gender, MppMeshType meshType) {
    if (gender == MppGender::CBBE) {
        switch (meshType) {
            case MppMeshType::Hands: return "Rley COR - CBBE Hands.osp";
            case MppMeshType::Feet:  return "Rley COR - CBBE Feet.osp";
            default:                 return "Rley COR - CBBE Body.osp";
        }
    }
    switch (meshType) {
        case MppMeshType::Hands: return "Rley HIMBO Hands.osp";
        case MppMeshType::Feet:  return "Rley HIMBO Feet.osp";
        default:                 return "Rley HIMBO BODY 2.osp";
    }
}

QString MppReferences::setName(MppGender gender, MppMeshType meshType) {
    if (gender == MppGender::CBBE) {
        switch (meshType) {
            case MppMeshType::Hands: return "Rley COR - CBBE Hands";
            case MppMeshType::Feet:  return "Rley COR - CBBE Feet";
            default:                 return "Rley COR - CBBE Body";
        }
    }
    switch (meshType) {
        case MppMeshType::Hands: return "Rley HIMBO Hands";
        case MppMeshType::Feet:  return "Rley HIMBO Feet";
        default:                 return "Rley HIMBO BODY 2";
    }
}

QString AutomationXmlWriter::generate(const PatchRunConfig& config) {
    QString output;
    QXmlStreamWriter xml(&output);
    xml.setAutoFormatting(true);
    xml.writeStartDocument();
    xml.writeStartElement("AutomationScript");

    // Batch settings — process all SliderSets matching our set names
    xml.writeStartElement("Batch");
    xml.writeAttribute("mode", "SliderSets");
    xml.writeTextElement("Folder", "CalienteTools/BodySlide/SliderSets");
    // Filter: join all SliderSet names as a regex alternation
    if (!config.sliderSetNames.isEmpty()) {
        QStringList escaped;
        for (const auto& name : config.sliderSetNames)
            escaped << QRegularExpression::escape(name);
        xml.writeTextElement("SliderSetFilter", "^(" + escaped.join("|") + ")$");
        xml.writeTextElement("SliderSetFilterRegex", "true");
    }
    xml.writeEndElement(); // Batch

    // Step 1: Remove old reference
    xml.writeStartElement("Step");
    xml.writeAttribute("type", "ClearReference");
    xml.writeEndElement();

    // Step 2: Load MPP reference
    QString refOsp = "CalienteTools/BodySlide/SliderSets/" +
                     MppReferences::ospName(config.gender, config.meshType);
    xml.writeStartElement("Step");
    xml.writeAttribute("type", "LoadReference");
    xml.writeTextElement("SourceFile", refOsp);
    xml.writeTextElement("Set", MppReferences::setName(config.gender, config.meshType));
    xml.writeTextElement("MergeSliders", "true");
    xml.writeEndElement();

    // Step 3: Conform All for RM_* sliders only
    xml.writeStartElement("Step");
    xml.writeAttribute("type", "ConformSliders");
    xml.writeTextElement("SliderNames", "RM_Heavy,RM_Muscular,RM_Thin");
    xml.writeTextElement("ProximityRadius", "10.0");
    xml.writeTextElement("MaxResults", "10");
    xml.writeEndElement();

    // Step 4: Save project
    xml.writeStartElement("Step");
    xml.writeAttribute("type", "SaveProject");
    xml.writeEndElement();

    xml.writeEndElement(); // AutomationScript
    xml.writeEndDocument();
    return output;
}

QString AutomationXmlWriter::writeToFolder(const PatchRunConfig& config,
                                            const QString& automationsFolder) {
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString scriptName = "mppatcher_" + timestamp;
    QString filePath = automationsFolder + "/" + scriptName + ".xml";

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return {};
    file.write(generate(config).toUtf8());
    return scriptName;
}
```

- [ ] **Step 3: Write failing test**

```cpp
// tests/test_automation_xml_writer.cpp
#include <catch2/catch_test_macros.hpp>
#include "AutomationXmlWriter.h"
#include <QDomDocument>

TEST_CASE("AutomationXmlWriter generates valid XML with correct steps", "[axw]") {
    PatchRunConfig cfg;
    cfg.sliderSetNames = {"Rley 3BA Vanilla - Body - Ancient Nord"};
    cfg.gender = MppGender::CBBE;
    cfg.meshType = MppMeshType::Body;

    QString xml = AutomationXmlWriter::generate(cfg);
    REQUIRE(!xml.isEmpty());

    QDomDocument doc;
    REQUIRE(doc.setContent(xml));

    auto root = doc.documentElement();
    REQUIRE(root.tagName() == "AutomationScript");

    // Verify Batch element
    auto batch = root.firstChildElement("Batch");
    REQUIRE(!batch.isNull());
    REQUIRE(batch.attribute("mode") == "SliderSets");

    // Verify steps: ClearReference, LoadReference, ConformSliders, SaveProject
    auto steps = root.elementsByTagName("Step");
    REQUIRE(steps.count() == 4);
    REQUIRE(steps.at(0).toElement().attribute("type") == "ClearReference");
    REQUIRE(steps.at(1).toElement().attribute("type") == "LoadReference");
    REQUIRE(steps.at(2).toElement().attribute("type") == "ConformSliders");
    REQUIRE(steps.at(3).toElement().attribute("type") == "SaveProject");

    // Verify LoadReference has correct set name
    auto loadRef = steps.at(1).toElement();
    REQUIRE(loadRef.firstChildElement("Set").text() == "Rley COR - CBBE Body");

    // Verify ConformSliders targets only RM_* sliders
    auto conform = steps.at(2).toElement();
    REQUIRE(conform.firstChildElement("SliderNames").text() == "RM_Heavy,RM_Muscular,RM_Thin");
}

TEST_CASE("MppReferences returns correct names", "[axw]") {
    REQUIRE(MppReferences::setName(MppGender::CBBE,  MppMeshType::Body)  == "Rley COR - CBBE Body");
    REQUIRE(MppReferences::setName(MppGender::CBBE,  MppMeshType::Hands) == "Rley COR - CBBE Hands");
    REQUIRE(MppReferences::setName(MppGender::HIMBO, MppMeshType::Feet)  == "Rley HIMBO Feet");
    REQUIRE(MppReferences::setName(MppGender::HIMBO, MppMeshType::Body)  == "Rley HIMBO BODY 2");
}
```

- [ ] **Step 4: Run test, verify fail → implement → verify pass**

```powershell
cmake --build build --target mppatcher-tests && ctest --test-dir build -R test_automation --output-on-failure
```

- [ ] **Step 5: Commit**

```powershell
git add src/AutomationXmlWriter.h src/AutomationXmlWriter.cpp tests/
git commit -m "feat: add AutomationXmlWriter with unit tests"
```

---

## Task 5: OsRunner

**Files:**
- Create: `src/OsRunner.h`
- Create: `src/OsRunner.cpp`

- [ ] **Step 1: Create `src/OsRunner.h`**

```cpp
#pragma once
#include <QObject>
#include <QString>
#include <QProcess>

class OsRunner : public QObject {
    Q_OBJECT
public:
    explicit OsRunner(QObject* parent = nullptr);

    void setOutfitStudioPath(const QString& exePath);
    void setAutomationsFolder(const QString& folder);

    // Runs OutfitStudio.exe -automation <scriptName> asynchronously
    // Emits finished(exitCode) when done
    void run(const QString& scriptName);

    bool isRunning() const;
    void cancel();

signals:
    void logLine(const QString& line);   // stdout/stderr line from OS
    void finished(int exitCode);

private:
    QString m_exePath;
    QString m_automationsFolder;
    QProcess* m_process = nullptr;
};
```

- [ ] **Step 2: Create `src/OsRunner.cpp`**

```cpp
#include "OsRunner.h"
#include <QFileInfo>

OsRunner::OsRunner(QObject* parent) : QObject(parent) {}

void OsRunner::setOutfitStudioPath(const QString& exePath) { m_exePath = exePath; }
void OsRunner::setAutomationsFolder(const QString& folder) { m_automationsFolder = folder; }
bool OsRunner::isRunning() const { return m_process && m_process->state() != QProcess::NotRunning; }

void OsRunner::run(const QString& scriptName) {
    if (isRunning()) return;

    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::MergedChannels);

    connect(m_process, &QProcess::readyRead, this, [this]() {
        while (m_process->canReadLine())
            emit logLine(QString::fromLocal8Bit(m_process->readLine()).trimmed());
    });

    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus) {
        emit finished(code);
        m_process->deleteLater();
        m_process = nullptr;
    });

    QStringList args = {"-automation", scriptName};
    m_process->start(m_exePath, args);
}

void OsRunner::cancel() {
    if (m_process) m_process->kill();
}
```

- [ ] **Step 3: Wire into MainWindow — run POC script via UI**

Add a "Test Run" button to MainWindow temporarily:
```cpp
// In MainWindow.cpp constructor:
auto* btn = new QPushButton("Test Run POC", this);
connect(btn, &QPushButton::clicked, this, [this]() {
    OsRunner* runner = new OsRunner(this);
    runner->setOutfitStudioPath(m_config.outfitStudioPath());
    runner->setAutomationsFolder(m_config.automationsFolder());
    connect(runner, &OsRunner::logLine, this, [](const QString& l){ qDebug() << l; });
    connect(runner, &OsRunner::finished, this, [](int code){ qDebug() << "Exit:" << code; });
    runner->run("mppatcher_poc");  // script from Task 2
});
```

- [ ] **Step 4: Verify "Test Run" triggers OS headlessly**

Build and run MPPatcher. Click "Test Run POC". Expected: OutfitStudio runs headlessly, qDebug shows log lines, exits with 0.

- [ ] **Step 5: Commit**

```powershell
git add src/OsRunner.h src/OsRunner.cpp src/MainWindow.cpp
git commit -m "feat: add OsRunner subprocess wrapper"
```

---

## Task 6: Main Window UI

**Files:**
- Modify: `src/MainWindow.h`
- Modify: `src/MainWindow.cpp`

- [ ] **Step 1: Add Config class for settings persistence**

```cpp
// src/Config.h
#pragma once
#include <QString>
#include <QSettings>

class Config {
public:
    Config();
    QString bodySlideDataPath() const;
    void setBodySlideDataPath(const QString& v);
    QString outfitStudioPath() const;
    void setOutfitStudioPath(const QString& v);
    QString outputFolder() const;
    void setOutputFolder(const QString& v);
    QString backupFolder() const;
    void setBackupFolder(const QString& v);
    bool newProjectMode() const;
    void setNewProjectMode(bool v);
private:
    QSettings m_settings;
};
```

```cpp
// src/Config.cpp
#include "Config.h"
Config::Config() : m_settings("ModernPhysiquePicker", "MPPatcher") {}
QString Config::bodySlideDataPath() const { return m_settings.value("bodySlideDataPath").toString(); }
void Config::setBodySlideDataPath(const QString& v) { m_settings.setValue("bodySlideDataPath", v); }
QString Config::outfitStudioPath() const { return m_settings.value("outfitStudioPath").toString(); }
void Config::setOutfitStudioPath(const QString& v) { m_settings.setValue("outfitStudioPath", v); }
QString Config::outputFolder() const { return m_settings.value("outputFolder").toString(); }
void Config::setOutputFolder(const QString& v) { m_settings.setValue("outputFolder", v); }
QString Config::backupFolder() const { return m_settings.value("backupFolder").toString(); }
void Config::setBackupFolder(const QString& v) { m_settings.setValue("backupFolder", v); }
bool Config::newProjectMode() const { return m_settings.value("newProjectMode", true).toBool(); }
void Config::setNewProjectMode(bool v) { m_settings.setValue("newProjectMode", v); }
```

- [ ] **Step 2: Implement `MainWindow` with full UI layout**

```cpp
// src/MainWindow.h
#pragma once
#include <QMainWindow>
#include "Config.h"
#include "SliderGroupReader.h"
#include "AutomationXmlWriter.h"
#include "OsRunner.h"

class QListWidget;
class QRadioButton;
class QLineEdit;
class QCheckBox;
class QPushButton;
class QProgressBar;
class QLabel;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void onPatch();
    void onRestoreSelected();
    void onBrowseBodySlide();
    void onBrowseOutputFolder();
    void onBrowseBackupFolder();
    void onOsLogLine(const QString& line);
    void onOsFinished(int exitCode);

private:
    void buildUi();
    void loadGroupList();
    void refreshBackupList();
    QStringList selectedGroupMembers() const;
    QString automationsFolder() const;

    Config m_config;
    OsRunner* m_runner = nullptr;

    // UI widgets
    QLineEdit* m_bsPathEdit = nullptr;
    QListWidget* m_groupList = nullptr;
    QRadioButton* m_radioNewProject = nullptr;
    QRadioButton* m_radioOverwrite = nullptr;
    QLineEdit* m_outputFolderEdit = nullptr;
    QCheckBox* m_backupCheck = nullptr;
    QLineEdit* m_backupFolderEdit = nullptr;
    QPushButton* m_patchBtn = nullptr;
    QProgressBar* m_progress = nullptr;
    QLabel* m_statusLabel = nullptr;
    QListWidget* m_backupList = nullptr;
    QPushButton* m_restoreBtn = nullptr;
};
```

Implement `buildUi()` to create the layout matching the spec mockup:
- Path row: "BodySlide path:" label + QLineEdit + browse button
- Group list: QListWidget with checkboxes (Qt::ItemIsUserCheckable)
- Output mode: QRadioButton group + conditional folder pickers
- Patch button + QProgressBar
- Backup list + Restore button

- [ ] **Step 3: Implement `onPatch()` slot**

```cpp
void MainWindow::onPatch() {
    QStringList members = selectedGroupMembers();
    if (members.isEmpty()) {
        QMessageBox::warning(this, "No Groups", "Select at least one Slider Group.");
        return;
    }

    // Determine gender from group names (HIMBO keyword = male)
    bool hasHimbo = false;
    for (int i = 0; i < m_groupList->count(); i++) {
        auto* item = m_groupList->item(i);
        if (item->checkState() == Qt::Checked &&
            item->text().contains("himbo", Qt::CaseInsensitive))
            hasHimbo = true;
    }

    PatchRunConfig cfg;
    cfg.sliderSetNames = members;
    cfg.gender = hasHimbo ? MppGender::HIMBO : MppGender::CBBE;
    cfg.meshType = MppMeshType::Body;  // extend later for hands/feet
    cfg.outputFolder = m_radioNewProject->isChecked() ? m_outputFolderEdit->text() : "";

    // Generate and write XML
    QString automFolder = automationsFolder();
    QString scriptName = AutomationXmlWriter::writeToFolder(cfg, automFolder);
    if (scriptName.isEmpty()) {
        QMessageBox::critical(this, "Error", "Failed to write automation script.");
        return;
    }

    m_patchBtn->setEnabled(false);
    m_progress->setRange(0, 0);  // indeterminate
    m_statusLabel->setText("Running...");

    m_runner = new OsRunner(this);
    m_runner->setOutfitStudioPath(m_config.outfitStudioPath());
    m_runner->setAutomationsFolder(automFolder);
    connect(m_runner, &OsRunner::logLine, this, &MainWindow::onOsLogLine);
    connect(m_runner, &OsRunner::finished, this, &MainWindow::onOsFinished);
    m_runner->run(scriptName);
}

void MainWindow::onOsFinished(int exitCode) {
    m_patchBtn->setEnabled(true);
    m_progress->setRange(0, 1);
    m_progress->setValue(1);
    if (exitCode == 0)
        m_statusLabel->setText("Patch complete.");
    else
        m_statusLabel->setText(QString("OutfitStudio exited with code %1. Check the log.").arg(exitCode));
    refreshBackupList();
}
```

- [ ] **Step 4: Build and test UI manually**

Build, run MPPatcher. Select one group, click Patch. Verify OutfitStudio runs headlessly and the outfit gets patched.

- [ ] **Step 5: Commit**

```powershell
git add src/
git commit -m "feat: implement main window UI with patch workflow"
```

---

## Task 7: BackupManager

**Files:**
- Create: `src/BackupManager.h`
- Create: `src/BackupManager.cpp`

Add QuaZip to CMakeLists.txt for zip support.

- [ ] **Step 1: Add QuaZip dependency to CMakeLists.txt**

```cmake
find_package(QuaZip-Qt5 REQUIRED)
target_link_libraries(MPPatcher PRIVATE QuaZip::QuaZip)
```

Install QuaZip: `vcpkg install quazip[qt5]:x64-windows` or download from GitHub.

- [ ] **Step 2: Create `src/BackupManager.h`**

```cpp
#pragma once
#include <QString>
#include <QStringList>

struct BackupEntry {
    QString timestamp;              // "2026-05-28 18:30"
    QString filePath;               // full path to the .zip
    QStringList originalPaths;      // paths of files inside the zip
    int fileCount;
};

class BackupManager {
public:
    // Zip all .osp and .osd files in the given paths list
    // Returns the path to the created zip file, or empty on failure
    static QString createBackup(const QStringList& filePaths, const QString& backupFolder);

    // List all available backups in a folder, sorted by date descending
    static QList<BackupEntry> listBackups(const QString& backupFolder);

    // Restore a backup — extract zip to original paths, returns false on failure
    static bool restoreBackup(const QString& zipPath);
};
```

- [ ] **Step 3: Implement `BackupManager.cpp`**

```cpp
#include "BackupManager.h"
#include <quazip/quazip.h>
#include <quazip/quazipfile.h>
#include <QDateTime>
#include <QDir>
#include <QFile>

QString BackupManager::createBackup(const QStringList& filePaths,
                                     const QString& backupFolder) {
    QDir().mkpath(backupFolder);
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString zipPath = backupFolder + "/MPPatcher_backup_" + timestamp + ".zip";

    QuaZip zip(zipPath);
    if (!zip.open(QuaZip::mdCreate)) return {};

    for (const auto& src : filePaths) {
        QFile f(src);
        if (!f.open(QIODevice::ReadOnly)) continue;

        QuaZipFile zipFile(&zip);
        // Store with relative path inside zip
        if (!zipFile.open(QIODevice::WriteOnly,
                          QuaZipNewInfo(QFileInfo(src).absoluteFilePath(), src)))
            continue;
        zipFile.write(f.readAll());
        zipFile.close();
        f.close();
    }
    zip.close();
    return zipPath;
}

bool BackupManager::restoreBackup(const QString& zipPath) {
    QuaZip zip(zipPath);
    if (!zip.open(QuaZip::mdUnzip)) return false;

    bool ok = true;
    for (bool more = zip.goToFirstFile(); more; more = zip.goToNextFile()) {
        QuaZipFile zipFile(&zip);
        if (!zipFile.open(QIODevice::ReadOnly)) { ok = false; continue; }

        QString dest = zip.getCurrentFileName();  // stored as absolute path
        QDir().mkpath(QFileInfo(dest).absolutePath());
        QFile out(dest);
        if (!out.open(QIODevice::WriteOnly)) { ok = false; continue; }
        out.write(zipFile.readAll());
        zipFile.close();
    }
    zip.close();
    return ok;
}

QList<BackupEntry> BackupManager::listBackups(const QString& backupFolder) {
    QList<BackupEntry> result;
    QDir dir(backupFolder);
    auto files = dir.entryList({"MPPatcher_backup_*.zip"}, QDir::Files, QDir::Time);
    for (const auto& f : files) {
        BackupEntry entry;
        entry.filePath = dir.filePath(f);
        // Parse timestamp from filename: MPPatcher_backup_YYYYMMDD_HHMMSS.zip
        QString ts = f.section('_', 2, 3).section('.', 0, 0);
        QDateTime dt = QDateTime::fromString(ts, "yyyyMMdd_HHmmss");
        entry.timestamp = dt.toString("yyyy-MM-dd HH:mm:ss");
        entry.fileCount = 0;  // populate on demand
        result.append(entry);
    }
    return result;
}
```

- [ ] **Step 4: Wire into MainWindow — backup before overwrite + restore list**

In `onPatch()`, before running OS in overwrite mode:
```cpp
if (m_radioOverwrite->isChecked() && m_backupCheck->isChecked()) {
    // Collect all .osp + .osd files that will be modified
    QStringList toBackup;
    // ... collect from sliderSetsFolder for the selected SliderSets
    BackupManager::createBackup(toBackup, m_config.backupFolder());
    refreshBackupList();
}
```

In `refreshBackupList()`:
```cpp
void MainWindow::refreshBackupList() {
    m_backupList->clear();
    auto backups = BackupManager::listBackups(m_config.backupFolder());
    for (const auto& b : backups) {
        auto* item = new QListWidgetItem(
            b.timestamp + " — " + QString::number(b.fileCount) + " files");
        item->setData(Qt::UserRole, b.filePath);
        m_backupList->addItem(item);
    }
}
```

In `onRestoreSelected()`:
```cpp
void MainWindow::onRestoreSelected() {
    auto* item = m_backupList->currentItem();
    if (!item) return;
    QString zipPath = item->data(Qt::UserRole).toString();
    if (QMessageBox::question(this, "Restore Backup",
        "Restore this backup? This will overwrite patched files.\n\n" + zipPath)
        != QMessageBox::Yes) return;
    if (!BackupManager::restoreBackup(zipPath))
        QMessageBox::critical(this, "Error", "Restore failed.");
    else
        QMessageBox::information(this, "Restored", "Backup restored successfully.");
}
```

- [ ] **Step 5: Commit**

```powershell
git add src/BackupManager.h src/BackupManager.cpp src/MainWindow.cpp CMakeLists.txt
git commit -m "feat: add BackupManager with zip backup/restore"
```

---

## Task 8: End-to-End Integration Test

- [ ] **Step 1: Run MPPatcher against all FOS female vanilla outfits**

```
1. Open MPPatcher.exe via MO2
2. Select "Rley 3BA Vanilla Outfits" (or equivalent group name)
3. Output mode: New Project, output folder = E:\tmp\mpp_e2e_output
4. Click Patch
```

Expected: process runs, exits 0, output folder contains patched .osp files.

- [ ] **Step 2: Open 5 random patched outfits in OutfitStudio**

Verify:
- `RM_Heavy`, `RM_Muscular`, `RM_Thin` sliders present on all shapes
- Original CBBE/3BA sliders untouched
- Reference shape = MPP body

- [ ] **Step 3: Test overwrite + restore flow**

```
1. Run again with Overwrite mode + backup enabled
2. Verify backup .zip created in backup folder
3. Click Restore Selected → verify files are restored to original state
4. Open a restored .osp in OutfitStudio → verify NO RM_* sliders (pre-patch state)
```

- [ ] **Step 4: Commit final state + tag**

```powershell
git add .
git commit -m "feat: complete MPPatcher v0.1 implementation"
git tag v0.1.0
```
