#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <tchar.h>
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <d3d11.h>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <functional>
#include <ctime>
#include <map>
#include "Student.h"

#pragma comment(linker, "/SUBSYSTEM:windows /ENTRY:WinMainCRTStartup")
#pragma comment(lib, "d3d11.lib")

static ID3D11Device* g_pd3dDevice = NULL;
static ID3D11DeviceContext* g_pd3dDeviceContext = NULL;
static IDXGISwapChain* g_pSwapChain = NULL;
static ID3D11RenderTargetView* g_mainRenderTargetView = NULL;

enum AppTab { TAB_JOURNAL, TAB_STATS, TAB_SETTINGS };

enum UserRole { ROLE_ADMIN, ROLE_TEACHER };

struct User {
    std::string login;
    size_t passwordHash = 0;
    UserRole role = ROLE_TEACHER;
    std::string assignedSubject;
};

struct AppConfig {
    std::vector<std::string> all_groups = {
        "Б2", "Б3", "Б101", "Б201", "Б301", "ВМ107", "ВМ207", "ВМ307",
        "ИТ105", "ИТ205", "ИТ209", "ИТ305", "ИТ309", "ИТ409", "М102",
        "М202", "М212", "ОД103", "ОД203", "ОД303", "ОД308", "П2", "П3",
        "П106", "П206", "П210", "П306", "П310", "СП405", "ТО10"
    };
    std::vector<std::string> filter_options;

    std::vector<std::string> grade_options = { "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "Н", "У", "Б" };
    std::vector<std::string> grade_types = { "Домашняя работа", "Практическая работа", "Экзамен", "Работа на уроке" };

    AppConfig() {
        filter_options.push_back("ВСЕ ГРУППЫ");
        for (const auto& g : all_groups) filter_options.push_back(g);
    }

    std::string getSpecialtyName(const std::string& group) const {
        if (group.find("П") == 0) return "Правоведение";
        if (group.find("ИТ") == 0) return "Разработка ПО";
        if (group.find("Б") == 0) return "Бухгалтерский учет";
        if (group.find("ОД") == 0) return "Логистика";
        if (group.find("М") == 0 || group.find("ТО") == 0 || group.find("ВМ") == 0) return "Коммерция";
        if (group.find("СП") == 0) return "Социальная работа";
        return "Общее отделение";
    }

    std::vector<std::string> getSubjectsForGroup(const std::string& group) const {
        std::vector<std::string> s = { "Математика", "Ин. язык", "Физкультура", "История", "Бел. язык" };
        if (group.find("П") == 0) s.insert(s.begin(), { "ТГП", "Гражданское право", "Уголовное право", "Трудовое право" });
        else if (group.find("ИТ") == 0) s.insert(s.begin(), { "ОАП", "Базы данных", "Комп. сети", "Веб-дизайн" });
        else if (group.find("Б") == 0) s.insert(s.begin(), { "Бух. учет", "Налоги", "Аудит" });
        else if (group.find("ОД") == 0) s.insert(s.begin(), { "Логистика", "Транспорт", "ВЭД" });
        s.push_back("Другое...");
        return s;
    }
};

struct Notification { std::string message; float timer; ImVec4 color; };

static std::string GetCurrentDateString() {
    auto now = std::chrono::system_clock::now();
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);
    struct tm parts;
    localtime_s(&parts, &now_c);
    char buf[32];
    sprintf_s(buf, "%02d.%02d.%04d", parts.tm_mday, parts.tm_mon + 1, parts.tm_year + 1900);
    return std::string(buf);
}

struct AppState {
    bool isLoggedIn = false;
    User currentUser;
    std::vector<User> systemUsers;

    AppTab currentTab = TAB_JOURNAL;
    std::vector<Student> journal;
    std::vector<Notification> notifications;
    AppConfig config;

    int currentSemester = 1;

    char searchBuf[128] = "";
    int groupFilterIdx = 0;
    int currentSortIdx = 0;

    bool openAdd = false;
    bool openDelete = false;
    bool openGrade = false;
    bool openView = false;
    bool openEdit = false;
    bool openTransferConfirm = false;

    char newName[128] = "";
    char newSpec[128] = "";
    int newGroupIdx = 0;

    int newGradeValueIdx = 4;
    int newGradeTypeIdx = 0;
    char newGradeDate[32] = "";

    int selectedStudentIdx = -1;
    int studentToDeleteIdx = -1;

    char editName[128] = "";
    char editSpec[128] = "";
    int editGroupIdx = 0;

    std::string transferNewGroup;
    std::string transferNewSpec;

    std::string editingSubject = "";
    int editingGradeIdx = -1;
    int editingGradeValueIdx = 4;
    int editingGradeTypeIdx = 0;
    char editingGradeDate[32] = "";

    void AddNotification(const std::string& msg, ImVec4 color = ImVec4(0.16f, 0.50f, 0.96f, 1.0f)) {
        notifications.push_back({ msg, 3.0f, color });
    }
};

static bool ComboStdString(const char* label, int* current_item, const std::vector<std::string>& items) {
    bool value_changed = false;
    const char* preview_value = (*current_item >= 0 && *current_item < (int)items.size()) ? items[*current_item].c_str() : "";
    if (ImGui::BeginCombo(label, preview_value)) {
        for (int i = 0; i < (int)items.size(); i++) {
            const bool is_selected = (*current_item == i);
            if (ImGui::Selectable(items[i].c_str(), is_selected)) { *current_item = i; value_changed = true; }
            if (is_selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return value_changed;
}

static std::string toLowerUTF8(const std::string& str) {
    std::string result = str;
    for (size_t i = 0; i < result.length(); ++i) {
        unsigned char c = result[i];
        if (c < 128) result[i] = (char)std::tolower(c);
        else if (c == 0xD0 && i + 1 < result.length()) {
            unsigned char c2 = result[i + 1];
            if (c2 >= 0x90 && c2 <= 0xAF) result[i + 1] = c2 + 0x20;
            else if (c2 == 0x81) { result[i] = (char)0xD1; result[i + 1] = (char)0x91; }
            i++;
        }
    }
    return result;
}

static void ApplySorting(AppState& state) {
    int sem = state.currentSemester;
    if (state.currentSortIdx == 1) std::sort(state.journal.begin(), state.journal.end(), [](const Student& a, const Student& b) { return a.fullName < b.fullName; });
    else if (state.currentSortIdx == 2) std::sort(state.journal.begin(), state.journal.end(), [](const Student& a, const Student& b) { return a.fullName > b.fullName; });
    else if (state.currentSortIdx == 3) std::sort(state.journal.begin(), state.journal.end(), [sem](const Student& a, const Student& b) { return a.getTotalAverage(sem) < b.getTotalAverage(sem); });
    else if (state.currentSortIdx == 4) std::sort(state.journal.begin(), state.journal.end(), [sem](const Student& a, const Student& b) { return a.getTotalAverage(sem) > b.getTotalAverage(sem); });
}

static void SaveToFile(const AppState& state) {
    std::ofstream file("students_pro.txt");
    if (!file.is_open()) return;

    file << state.systemUsers.size() << "\n";
    for (const auto& u : state.systemUsers) {
        file << std::quoted(u.login) << " " << u.passwordHash << " " << (int)u.role << " " << std::quoted(u.assignedSubject) << "\n";
    }

    file << state.journal.size() << "\n";
    for (const auto& s : state.journal) {
        file << std::quoted(s.fullName) << " " << std::quoted(s.group) << " " << std::quoted(s.specialty) << " "
            << s.semesterGrades.size() << " " << s.archiveGrades.size() << "\n";

        for (const auto& semPair : s.semesterGrades) {
            file << semPair.first << " " << semPair.second.size() << "\n";
            for (const auto& subPair : semPair.second) {
                file << std::quoted(subPair.first) << " " << subPair.second.size() << "\n";
                for (const auto& g : subPair.second) {
                    file << std::quoted(g.value) << " " << g.type << " " << std::quoted(g.date) << " ";
                }
                file << "\n";
            }
        }

        for (const auto& archPair : s.archiveGrades) {
            file << std::quoted(archPair.first) << " " << archPair.second.size() << "\n";
            for (const auto& g : archPair.second) {
                file << std::quoted(g.value) << " " << g.type << " " << std::quoted(g.date) << " ";
            }
            file << "\n";
        }
    }
}

static void LoadFromFile(AppState& state) {
    std::ifstream file("students_pro.txt");
    state.systemUsers.clear();
    state.journal.clear();

    if (!file.is_open()) {
        std::hash<std::string> hasher;
        state.systemUsers.push_back({ "admin", hasher("admin"), ROLE_ADMIN, "ALL" });
        state.systemUsers.push_back({ "teacher", hasher("123"), ROLE_TEACHER, "Математика" });
        return;
    }

    size_t usersCount = 0;
    if (file >> usersCount) {
        for (size_t i = 0; i < usersCount; ++i) {
            User u; int roleInt;
            file >> std::quoted(u.login) >> u.passwordHash >> roleInt >> std::quoted(u.assignedSubject);
            u.role = (UserRole)roleInt;
            state.systemUsers.push_back(u);
        }
    }

    size_t journalSize = 0;
    if (!(file >> journalSize)) return;

    for (size_t i = 0; i < journalSize; ++i) {
        Student s;
        size_t semestersCount = 0, archiveCount = 0;
        file >> std::quoted(s.fullName) >> std::quoted(s.group) >> std::quoted(s.specialty) >> semestersCount >> archiveCount;

        for (size_t j = 0; j < semestersCount; ++j) {
            int semId = 0; size_t subjectsCount = 0;
            file >> semId >> subjectsCount;
            for (size_t k = 0; k < subjectsCount; ++k) {
                std::string sub; size_t gradesCount = 0;
                file >> std::quoted(sub) >> gradesCount;
                for (size_t g = 0; g < gradesCount; ++g) {
                    GradeRecord gr;
                    file >> std::quoted(gr.value) >> gr.type >> std::quoted(gr.date);
                    s.semesterGrades[semId][sub].push_back(gr);
                }
            }
        }

        for (size_t j = 0; j < archiveCount; ++j) {
            std::string sub; size_t gradesCount = 0;
            file >> std::quoted(sub) >> gradesCount;
            for (size_t g = 0; g < gradesCount; ++g) {
                GradeRecord gr;
                file >> std::quoted(gr.value) >> gr.type >> std::quoted(gr.date);
                s.archiveGrades[sub].push_back(gr);
            }
        }
        state.journal.push_back(s);
    }
}

static void SetupFullScreenModernStyle() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0.0f; style.ChildRounding = 8.0f; style.FrameRounding = 6.0f; style.PopupRounding = 12.0f;
    style.WindowPadding = ImVec2(24, 24); style.FramePadding = ImVec2(14, 10); style.ItemSpacing = ImVec2(12, 12);
    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.06f, 0.08f, 1.00f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.11f, 0.11f, 0.13f, 1.00f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.14f, 0.14f, 0.17f, 0.99f);
    colors[ImGuiCol_Text] = ImVec4(0.95f, 0.95f, 0.97f, 1.00f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.55f, 0.55f, 0.60f, 1.0f);
    ImVec4 accent = ImVec4(0.16f, 0.50f, 0.96f, 1.0f);
    colors[ImGuiCol_Header] = ImVec4(accent.x, accent.y, accent.z, 0.25f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(accent.x, accent.y, accent.z, 0.45f);
    colors[ImGuiCol_HeaderActive] = accent;
    colors[ImGuiCol_Button] = ImVec4(accent.x, accent.y, accent.z, 0.75f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(accent.x, accent.y, accent.z, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.10f, 0.40f, 0.80f, 1.0f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.18f, 0.18f, 0.22f, 1.0f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.24f, 0.24f, 0.28f, 1.0f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.28f, 0.28f, 0.35f, 1.0f);
    colors[ImGuiCol_Separator] = ImVec4(0.25f, 0.25f, 0.30f, 1.0f);
    colors[ImGuiCol_PlotHistogram] = accent;
    colors[ImGuiCol_TableHeaderBg] = ImVec4(0.14f, 0.14f, 0.18f, 1.0f);
}

static void RenderAuthScreen(AppState& state, bool& done) {
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));

    ImGui::Begin("Auth", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize);

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.16f, 0.50f, 0.96f, 1.0f));
    ImGui::SetWindowFontScale(1.8f);
    ImGui::Text("MTEC JOURNAL LOGIN");
    ImGui::PopStyleColor();
    ImGui::SetWindowFontScale(1.0f);
    ImGui::TextDisabled("Авторизация в системе");
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

    float inputWidth = 360.0f;

    static char loginBuf[64] = "";
    static char passBuf[64] = "";
    static bool loginError = false;

    bool enterPressed = false;

    ImGui::Text("Логин:");
    ImGui::SetNextItemWidth(inputWidth);
    if (ImGui::InputText("##L", loginBuf, 64, ImGuiInputTextFlags_EnterReturnsTrue)) {
        enterPressed = true;
    }
    if (ImGui::IsItemEdited()) loginError = false;

    ImGui::Spacing();
    ImGui::Text("Пароль:");
    ImGui::SetNextItemWidth(inputWidth);
    if (ImGui::InputText("##P", passBuf, 64, ImGuiInputTextFlags_Password | ImGuiInputTextFlags_EnterReturnsTrue)) {
        enterPressed = true;
    }
    if (ImGui::IsItemEdited()) loginError = false;

    ImGui::Spacing();

    ImGui::TextDisabled(" * Подсказка: для входа нажмите клавишу Enter");
    ImGui::Spacing();

    if (loginError) {
        ImGui::TextColored(ImVec4(0.9f, 0.2f, 0.2f, 1.0f), "Неверный логин или пароль!");
    }
    else {
        ImGui::Text(" ");
    }

    ImGui::Spacing(); ImGui::Spacing();

    float btn_w = (inputWidth - ImGui::GetStyle().ItemSpacing.x) / 2.0f;

    bool btnClicked = ImGui::Button("ВОЙТИ В БАЗУ", ImVec2(btn_w, 40));
    ImGui::SameLine();

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 0.6f));
    if (ImGui::Button("ВЫЙТИ", ImVec2(btn_w, 40))) {
        done = true;
    }
    ImGui::PopStyleColor();

    if (btnClicked || enterPressed) {
        std::hash<std::string> hasher;
        size_t inputHash = hasher(std::string(passBuf));

        loginError = true;
        for (const auto& u : state.systemUsers) {
            if (u.login == loginBuf && u.passwordHash == inputHash) {
                state.currentUser = u;
                state.isLoggedIn = true;
                loginError = false;
                state.AddNotification("Вход выполнен: " + u.login);
                memset(passBuf, 0, sizeof(passBuf));
                break;
            }
        }
    }
    ImGui::End();
}

static void RenderSidebar(AppState& state, bool& done) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("Sidebar", ImVec2(250, 0), false);
    ImGui::Spacing(); ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.16f, 0.50f, 0.96f, 1.0f));
    ImGui::SetWindowFontScale(1.4f); ImGui::Text("  MTEC JOURNAL"); ImGui::PopStyleColor();

    std::string roleStr = (state.currentUser.role == ROLE_ADMIN) ? "Администратор" : "Преподаватель";
    ImGui::SetWindowFontScale(1.0f); ImGui::TextDisabled("    %s", roleStr.c_str());
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

    auto DrawSidebarBtn = [&state](const char* label, AppTab tabType) {
        bool selected = (state.currentTab == tabType);
        if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.50f, 0.96f, 0.4f));
        else ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.16f, 0.50f, 0.96f, 0.2f));
        if (ImGui::Button(label, ImVec2(250, 45))) state.currentTab = tabType;
        ImGui::PopStyleColor(2);
        };

    DrawSidebarBtn(" Журнал студентов", TAB_JOURNAL);
    DrawSidebarBtn(" Статистика", TAB_STATS);
    if (state.currentUser.role == ROLE_ADMIN) {
        DrawSidebarBtn(" Настройки", TAB_SETTINGS);
    }

    float bottom_space = 110.0f;
    float cursorY = ImGui::GetWindowHeight() - bottom_space;
    if (ImGui::GetCursorPosY() < cursorY) {
        ImGui::SetCursorPosY(cursorY);
    }
    else {
        ImGui::Spacing(); ImGui::Spacing();
    }

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 0.05f));
    if (ImGui::Button(" Выход из профиля", ImVec2(250, 45))) {
        SaveToFile(state);
        state.isLoggedIn = false;
    }
    ImGui::PopStyleColor();

    ImGui::Spacing();

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 0.3f));
    if (ImGui::Button(" Выйти из программы", ImVec2(250, 45))) {
        SaveToFile(state);
        done = true;
    }
    ImGui::PopStyleColor();

    ImGui::EndChild();
    ImGui::PopStyleVar();
}

static void RenderJournalTab(AppState& state) {
    ImGui::Text("Списки групп");

    float btnWidth = 130.0f;
    float totalBtnsWidth = btnWidth + ImGui::GetStyle().ItemSpacing.x + btnWidth;
    float center_x = (ImGui::GetContentRegionAvail().x - totalBtnsWidth) / 2.0f;
    if (center_x < 150.0f) center_x = 150.0f;

    ImGui::SameLine(center_x);

    if (state.currentSemester == 1) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.50f, 0.96f, 0.8f));
    else ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
    if (ImGui::Button("1 Семестр", ImVec2(btnWidth, 36))) state.currentSemester = 1;
    ImGui::PopStyleColor();

    ImGui::SameLine();
    if (state.currentSemester == 2) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.50f, 0.96f, 0.8f));
    else ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
    if (ImGui::Button("2 Семестр", ImVec2(btnWidth, 36))) state.currentSemester = 2;
    ImGui::PopStyleColor();

    if (state.currentUser.role == ROLE_ADMIN) {
        float add_btn_w = ImGui::CalcTextSize("+ ДОБАВИТЬ СТУДЕНТА").x + 32.0f;
        ImGui::SameLine(ImGui::GetContentRegionMax().x - add_btn_w);
        if (ImGui::Button("+ ДОБАВИТЬ СТУДЕНТА", ImVec2(add_btn_w, 38))) {
            memset(state.newName, 0, 128);
            strcpy_s(state.newSpec, "Не выбрана");
            state.openAdd = true;
        }
    }
    ImGui::Spacing();

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16, 16));
    ImGui::BeginChild("Toolbar", ImVec2(0, 72), true);
    float avail_w = ImGui::GetContentRegionAvail().x;
    ImGui::SetNextItemWidth((std::max)(200.0f, avail_w - 420.0f));
    ImGui::InputTextWithHint("##Search", "Поиск по ФИО...", state.searchBuf, 128);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(180);
    ComboStdString("##Filter", &state.groupFilterIdx, state.config.filter_options);
    ImGui::SameLine();
    std::vector<std::string> sorts = { "Сортировка", "А-Я", "Я-А", "Балл (возр.)", "Балл (убыв.)" };
    ImGui::SetNextItemWidth(200);
    if (ComboStdString("##Sort", &state.currentSortIdx, sorts)) ApplySorting(state);
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::Spacing();

    if (ImGui::BeginTable("MainJournal", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX | ImGuiTableFlags_BordersOuter, ImVec2(0, -10))) {
        ImGui::TableSetupColumn("ФИО СТУДЕНТА", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("ГР.", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("СПЕЦИАЛЬНОСТЬ", ImGuiTableColumnFlags_WidthFixed, 180.0f);
        ImGui::TableSetupColumn("БАЛЛ СЕМЕСТРА", ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableSetupColumn("ДЕЙСТВИЯ", ImGuiTableColumnFlags_WidthFixed, 440.0f);
        ImGui::TableHeadersRow();

        std::string searchStr = toLowerUTF8(state.searchBuf);
        for (int i = 0; i < (int)state.journal.size(); i++) {
            Student& s = state.journal[i];
            if (state.groupFilterIdx > 0 && s.group != state.config.filter_options[state.groupFilterIdx]) continue;
            if (strlen(state.searchBuf) > 0 && toLowerUTF8(s.fullName).find(searchStr) == std::string::npos) continue;

            ImGui::TableNextRow(0, 60.0f);

            ImGui::TableSetColumnIndex(0); ImGui::AlignTextToFramePadding(); ImGui::Text(" %s", s.fullName.c_str());
            ImGui::TableSetColumnIndex(1); ImGui::AlignTextToFramePadding();

            ImVec4 groupColor = ImVec4(0.2f, 0.8f, 0.4f, 1.0f);
            if (s.group.find("ИТ") == 0) groupColor = ImVec4(0.2f, 0.7f, 1.0f, 1.0f);
            if (s.group.find("Б") == 0) groupColor = ImVec4(1.0f, 0.7f, 0.2f, 1.0f);
            if (s.group.find("П") == 0) groupColor = ImVec4(0.8f, 0.4f, 1.0f, 1.0f);
            ImGui::TextColored(groupColor, "%s", s.group.c_str());

            ImGui::TableSetColumnIndex(2); ImGui::AlignTextToFramePadding(); ImGui::TextDisabled("%s", s.specialty.c_str());

            ImGui::TableSetColumnIndex(3);
            float avg = static_cast<float>(s.getTotalAverage(state.currentSemester));
            ImVec4 barColor = (avg >= 8.0f) ? ImVec4(0.1f, 0.8f, 0.3f, 1.0f) : (avg < 4.0f ? ImVec4(0.9f, 0.2f, 0.2f, 1.0f) : ImVec4(0.16f, 0.50f, 0.96f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, barColor);
            char avg_text[16]; sprintf_s(avg_text, "%.1f", avg);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 16);
            ImGui::ProgressBar(avg / 10.0f, ImVec2(-1, 24), avg_text);
            ImGui::PopStyleColor();

            ImGui::TableSetColumnIndex(4);
            ImGui::PushID(i);

            float btn_padding = 24.0f;
            float w1 = ImGui::CalcTextSize("+ ОЦЕНКА").x + btn_padding;
            float w2 = ImGui::CalcTextSize("ИНФО").x + btn_padding;
            float w3 = ImGui::CalcTextSize("ИЗМ.").x + btn_padding;
            float w4 = ImGui::CalcTextSize("УДАЛИТЬ").x + btn_padding;
            float spacing = 10.0f;

            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 12);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(spacing, 0));

            if (ImGui::Button("+ ОЦЕНКА", ImVec2(w1, 36))) {
                state.selectedStudentIdx = i;
                state.newGradeValueIdx = 4;
                state.newGradeTypeIdx = 0;
                strcpy_s(state.newGradeDate, GetCurrentDateString().c_str());
                state.openGrade = true;
            } ImGui::SameLine();

            if (ImGui::Button("ИНФО", ImVec2(w2, 36))) { state.selectedStudentIdx = i; state.openView = true; }

            if (state.currentUser.role == ROLE_ADMIN) {
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.4f, 0.4f, 0.4f, 0.4f));
                if (ImGui::Button("ИЗМ.", ImVec2(w3, 36))) {
                    state.selectedStudentIdx = i;
                    strcpy_s(state.editName, s.fullName.c_str()); strcpy_s(state.editSpec, s.specialty.c_str());
                    for (size_t g = 0; g < state.config.all_groups.size(); g++) {
                        if (state.config.all_groups[g] == s.group) state.editGroupIdx = (int)g;
                    }
                    state.openEdit = true;
                }
                ImGui::PopStyleColor(); ImGui::SameLine();

                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 0.6f));
                if (ImGui::Button("УДАЛИТЬ", ImVec2(w4, 36))) { state.studentToDeleteIdx = i; state.openDelete = true; }
                ImGui::PopStyleColor();
            }

            ImGui::PopStyleVar();
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

static void RenderStatsTab(AppState& state) {
    ImGui::Text("Глобальная статистика (%d семестр)", state.currentSemester);
    ImGui::Separator();
    ImGui::Spacing();

    int total = (int)state.journal.size();
    double globalAvg = 0;
    for (auto& s : state.journal) {
        globalAvg += s.getTotalAverage(state.currentSemester);
    }
    globalAvg = total > 0 ? globalAvg / total : 0;

    ImGui::BeginChild("Cards", ImVec2(0, 140), false);
    ImGui::Columns(3, "stats_cols", false);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16, 16));

    ImGui::BeginChild("C1", ImVec2(0, 120), true);
    ImGui::TextDisabled("Всего студентов:"); ImGui::Spacing();
    ImGui::SetWindowFontScale(2.0f); ImGui::Text("%d", total); ImGui::SetWindowFontScale(1.0f);
    ImGui::EndChild(); ImGui::NextColumn();

    ImGui::BeginChild("C2", ImVec2(0, 120), true);
    ImGui::TextDisabled("Средний балл (по колледжу):"); ImGui::Spacing();
    ImGui::SetWindowFontScale(2.0f); ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.4f, 1.0f), "%.2f", globalAvg); ImGui::SetWindowFontScale(1.0f);
    ImGui::EndChild(); ImGui::NextColumn();

    ImGui::BeginChild("C3", ImVec2(0, 120), true);
    ImGui::TextDisabled("Количество групп:"); ImGui::Spacing();
    ImGui::SetWindowFontScale(2.0f); ImGui::Text("%d", (int)state.config.all_groups.size()); ImGui::SetWindowFontScale(1.0f);
    ImGui::EndChild();

    ImGui::PopStyleVar();
    ImGui::Columns(1);
    ImGui::EndChild();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    struct GroupStat {
        std::string name;       
        int studentCount = 0;  
        double totalScore = 0.0;
        float avgScore = 0.0f;  
    };

    std::map<std::string, GroupStat> groupStats;
    for (const auto& s : state.journal) {
        groupStats[s.group].name = s.group;
        groupStats[s.group].studentCount++;
        groupStats[s.group].totalScore += s.getTotalAverage(state.currentSemester);
    }

    std::vector<GroupStat> activeGroups;
    for (auto& pair : groupStats) {
        if (pair.second.studentCount > 0) { 
            pair.second.avgScore = (float)(pair.second.totalScore / pair.second.studentCount);
            activeGroups.push_back(pair.second);
        }
    }

    std::sort(activeGroups.begin(), activeGroups.end(), [](const GroupStat& a, const GroupStat& b) {
        return a.avgScore > b.avgScore;
        });

    if (ImGui::BeginTable("StatsBottom", 2, ImGuiTableFlags_Resizable, ImVec2(0, -10))) {
        ImGui::TableSetupColumn("Визуализация", ImGuiTableColumnFlags_WidthStretch, 0.6f);
        ImGui::TableSetupColumn("Таблица", ImGuiTableColumnFlags_WidthStretch, 0.4f);
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        ImGui::TextColored(ImVec4(0.16f, 0.50f, 0.96f, 1.0f), "Рейтинг групп (Топ-10 по среднему баллу)");
        ImGui::Spacing();

        if (activeGroups.empty()) {
            ImGui::TextDisabled("Пока нет студентов с оценками для построения графика.");
        }
        else {
            int displayCount = (std::min)((int)activeGroups.size(), 10);

            for (int i = 0; i < displayCount; i++) {
                ImGui::Text("%-10s", activeGroups[i].name.c_str()); 
                ImGui::SameLine(80.0f); 

                ImVec4 barColor = ImVec4(0.16f, 0.50f, 0.96f, 1.0f); 
                if (activeGroups[i].avgScore >= 8.0f) barColor = ImVec4(0.2f, 0.8f, 0.4f, 1.0f); 
                else if (activeGroups[i].avgScore < 4.0f) barColor = ImVec4(0.9f, 0.2f, 0.2f, 1.0f); 

                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, barColor); 

                char buf[32]; 
                sprintf_s(buf, "%.2f", activeGroups[i].avgScore);

                ImGui::ProgressBar(activeGroups[i].avgScore / 10.0f, ImVec2(-1, 24), buf);

                ImGui::PopStyleColor(); 
                ImGui::Spacing();
            }
        }

        ImGui::TableSetColumnIndex(1);
        ImGui::TextColored(ImVec4(0.16f, 0.50f, 0.96f, 1.0f), "Сводка по всем группам");
        ImGui::Spacing();

        if (ImGui::BeginTable("GroupStatsTable", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY, ImVec2(0, -1))) {
            ImGui::TableSetupColumn("Группа", ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableSetupColumn("Студ.", ImGuiTableColumnFlags_WidthFixed, 50.0f);
            ImGui::TableSetupColumn("Ср.Балл", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();

            for (const auto& g : activeGroups) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::Text("%s", g.name.c_str());
                ImGui::TableSetColumnIndex(1); ImGui::Text("%d", g.studentCount);

                ImGui::TableSetColumnIndex(2);
                ImVec4 scoreColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f); 
                if (g.avgScore >= 8.0f) scoreColor = ImVec4(0.2f, 0.8f, 0.4f, 1.0f);
                else if (g.avgScore < 4.0f) scoreColor = ImVec4(0.9f, 0.2f, 0.2f, 1.0f);

                ImGui::TextColored(scoreColor, "%.2f", g.avgScore); 
            }
            ImGui::EndTable();
        }

        ImGui::EndTable(); 
    }
}



static void RenderSettingsTab(AppState& state) {
    ImGui::Text("Настройки"); ImGui::Separator(); ImGui::Spacing();
    if (ImGui::Button("СОХРАНИТЬ ДАННЫЕ В ФАЙЛ", ImVec2(400, 50))) { SaveToFile(state); state.AddNotification("База данных сохранена успешно"); }
}

static void RenderModals(AppState& state) {
    if (state.openAdd) { ImGui::OpenPopup("AddModal"); state.openAdd = false; }
    if (state.openDelete) { ImGui::OpenPopup("ConfirmDelete"); state.openDelete = false; }
    if (state.openGrade) { ImGui::OpenPopup("GradeP"); state.openGrade = false; }
    if (state.openView) { ImGui::OpenPopup("ViewP"); state.openView = false; }
    if (state.openEdit) { ImGui::OpenPopup("EditStudentModal"); state.openEdit = false; }
    if (state.openTransferConfirm) { ImGui::OpenPopup("TransferModal"); state.openTransferConfirm = false; }

    ImGui::SetNextWindowSizeConstraints(ImVec2(500, -1), ImVec2(800, 800));
    if (ImGui::BeginPopupModal("AddModal", NULL, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
        ImGui::TextColored(ImVec4(0.16f, 0.50f, 0.96f, 1.0f), "НОВЫЙ СТУДЕНТ"); ImGui::Separator(); ImGui::Spacing();
        ImGui::TextDisabled("ФИО:"); ImGui::SetNextItemWidth(-1); ImGui::InputText("##n", state.newName, 128); ImGui::Spacing();
        ImGui::TextDisabled("Группа:"); ImGui::SetNextItemWidth(-1);
        if (ComboStdString("##g", &state.newGroupIdx, state.config.all_groups)) {
            strcpy_s(state.newSpec, state.config.getSpecialtyName(state.config.all_groups[state.newGroupIdx]).c_str());
        }
        ImGui::Spacing();
        ImGui::TextDisabled("Специальность:"); ImGui::SetNextItemWidth(-1); ImGui::InputText("##s", state.newSpec, 128); ImGui::Spacing();
        ImGui::Separator(); ImGui::Spacing();
        float btn_w = static_cast<float>((ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2.0f);
        if (ImGui::Button("ДОБАВИТЬ", ImVec2(btn_w, 46))) {
            if (strlen(state.newName) > 1) {
                Student s;
                s.fullName = state.newName;
                s.group = state.config.all_groups[state.newGroupIdx];
                s.specialty = state.newSpec;
                state.journal.push_back(s);
                SaveToFile(state);
                state.AddNotification("Студент добавлен");
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine(); if (ImGui::Button("ОТМЕНА", ImVec2(btn_w, 46))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowSizeConstraints(ImVec2(450, -1), ImVec2(600, 600));
    if (ImGui::BeginPopupModal("GradeP", NULL, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
        if (state.selectedStudentIdx >= 0) {
            auto allSubs = state.config.getSubjectsForGroup(state.journal[state.selectedStudentIdx].group);
            std::vector<std::string> subs;

            if (state.currentUser.role == ROLE_TEACHER && state.currentUser.assignedSubject != "ALL") {
                if (std::find(allSubs.begin(), allSubs.end(), state.currentUser.assignedSubject) != allSubs.end()) {
                    subs.push_back(state.currentUser.assignedSubject);
                }
            }
            else {
                subs = allSubs;
            }

            if (subs.empty()) {
                ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1.0f), "Предмет учителя не найден в плане группы!");
                if (ImGui::Button("ЗАКРЫТЬ", ImVec2(-1, 46))) ImGui::CloseCurrentPopup();
            }
            else {
                static int subIdx = 0;
                if (subIdx >= subs.size()) subIdx = 0;

                ImGui::TextColored(ImVec4(0.16f, 0.50f, 0.96f, 1.0f), "ВЫСТАВИТЬ ОЦЕНКУ (Семестр %d)", state.currentSemester); ImGui::Separator(); ImGui::Spacing();
                ImGui::TextDisabled("Предмет:"); ImGui::SetNextItemWidth(-1); ComboStdString("##sub", &subIdx, subs); ImGui::Spacing();

                ImGui::TextDisabled("Дата выставления:");
                ImGui::SetNextItemWidth(-50);
                ImGui::InputText("##gdate", state.newGradeDate, 32);
                ImGui::SameLine();
                if (ImGui::Button("...", ImVec2(40, 0))) {
                    ImGui::OpenPopup("CalendarPopup");
                }

                if (ImGui::BeginPopup("CalendarPopup")) {
                    static int sel_d = 1, sel_m = 1, sel_y = 2026;

                    if (ImGui::IsWindowAppearing()) {
                        int pd = 1, pm = 1, py = 2026;
                        if (sscanf_s(state.newGradeDate, "%d.%d.%d", &pd, &pm, &py) == 3) {
                            sel_d = pd; sel_m = pm; sel_y = py;
                        }
                        else {
                            auto now = std::chrono::system_clock::now();
                            std::time_t now_c = std::chrono::system_clock::to_time_t(now);
                            struct tm parts; localtime_s(&parts, &now_c);
                            sel_d = parts.tm_mday; sel_m = parts.tm_mon + 1; sel_y = parts.tm_year + 1900;
                        }
                    }

                    const char* months[] = { "Январь", "Февраль", "Март", "Апрель", "Май", "Июнь", "Июль", "Август", "Сентябрь", "Октябрь", "Ноябрь", "Декабрь" };

                    if (ImGui::Button("<", ImVec2(30, 0))) { sel_m--; if (sel_m < 1) { sel_m = 12; sel_y--; } }
                    ImGui::SameLine();

                    char titleBuf[64]; sprintf_s(titleBuf, "%s %d", months[sel_m - 1], sel_y);

                    float windowWidth = ImGui::GetWindowSize().x;
                    float textWidth = ImGui::CalcTextSize(titleBuf).x;

                    ImGui::SameLine((windowWidth - textWidth) / 2.0f);
                    ImGui::Text("%s", titleBuf);

                    ImGui::SameLine(windowWidth - 30.0f - ImGui::GetStyle().WindowPadding.x);
                    if (ImGui::Button(">", ImVec2(30, 0))) { sel_m++; if (sel_m > 12) { sel_m = 1; sel_y++; } }

                    ImGui::Separator();

                    if (ImGui::BeginTable("cal_table", 7, ImGuiTableFlags_SizingStretchSame)) {
                        const char* days[] = { "Пн", "Вт", "Ср", "Чт", "Пт", "Сб", "Вс" };
                        for (int i = 0; i < 7; i++) {
                            ImGui::TableSetupColumn(days[i]);
                        }
                        ImGui::TableHeadersRow();

                        auto get_dow = [](int d, int m, int y) {
                            if (m < 3) { m += 12; y -= 1; }
                            int k = y % 100; int j = y / 100;
                            int f = d + ((13 * (m + 1)) / 5) + k + (k / 4) + (j / 4) + (5 * j);
                            return (f % 7 + 5) % 7;
                            };
                        auto days_in_month = [](int m, int y) {
                            if (m == 2) return (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 29 : 28;
                            if (m == 4 || m == 6 || m == 9 || m == 11) return 30;
                            return 31;
                            };

                        int start_dow = get_dow(1, sel_m, sel_y);
                        int total_days = days_in_month(sel_m, sel_y);

                        ImGui::TableNextRow();
                        for (int i = 0; i < start_dow; i++) {
                            ImGui::TableNextColumn();
                        }

                        for (int d = 1; d <= total_days; d++) {
                            ImGui::TableNextColumn();
                            char buf[8]; sprintf_s(buf, "%d", d);

                            bool is_selected = (d == sel_d);
                            if (is_selected) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.50f, 0.96f, 1.0f));
                            else ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));

                            if (ImGui::Button(buf, ImVec2(-1, 28))) {
                                sel_d = d;
                                sprintf_s(state.newGradeDate, 32, "%02d.%02d.%04d", sel_d, sel_m, sel_y);
                                ImGui::CloseCurrentPopup();
                            }
                            ImGui::PopStyleColor();
                        }
                        ImGui::EndTable();
                    }
                    ImGui::EndPopup();
                }
                ImGui::Spacing();

                ImGui::TextDisabled("Оценка (1-10):"); ImGui::SetNextItemWidth(-1);
                std::string previewText = state.config.grade_options[state.newGradeValueIdx];
                if (ImGui::BeginCombo("##gr", previewText.c_str())) {
                    for (int i = 0; i < 10; i++) {
                        bool is_selected = (state.newGradeValueIdx == i);
                        if (ImGui::Selectable(state.config.grade_options[i].c_str(), is_selected)) {
                            state.newGradeValueIdx = i;
                        }
                        if (is_selected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                ImGui::Spacing();

                ImGui::TextDisabled("Или отметка об отсутствии:");
                float btn_w2 = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2.0f;
                auto DrawLetterBtn = [&](int idx, const char* label) {
                    bool selected = (state.newGradeValueIdx == idx);
                    if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.3f, 0.3f, 1.0f));
                    else ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
                    if (ImGui::Button(label, ImVec2(btn_w2, 36))) state.newGradeValueIdx = idx;
                    ImGui::PopStyleColor();
                    };

                DrawLetterBtn(10, "Н (Неуваж.)"); ImGui::SameLine();
                DrawLetterBtn(11, "У (Уваж. / Болезнь)");
                ImGui::Spacing(); ImGui::Spacing();

                ImGui::TextDisabled("Тип работы:"); ImGui::SetNextItemWidth(-1);
                ComboStdString("##gt", &state.newGradeTypeIdx, state.config.grade_types); ImGui::Spacing();

                ImGui::Separator(); ImGui::Spacing();

                float btn_w = static_cast<float>((ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2.0f);
                if (ImGui::Button("ВЫСТАВИТЬ", ImVec2(btn_w, 46))) {
                    GradeRecord gr;
                    gr.value = state.config.grade_options[state.newGradeValueIdx];
                    gr.type = state.newGradeTypeIdx;
                    gr.date = state.newGradeDate;

                    state.journal[state.selectedStudentIdx].semesterGrades[state.currentSemester][subs[subIdx]].push_back(gr);
                    SaveToFile(state);
                    state.AddNotification("Оценка сохранена");
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine(); if (ImGui::Button("ОТМЕНА", ImVec2(btn_w, 46))) ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowSizeConstraints(ImVec2(600, -1), ImVec2(800, 800));
    if (ImGui::BeginPopupModal("ViewP", NULL, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
        if (state.selectedStudentIdx >= 0) {
            Student& s = state.journal[state.selectedStudentIdx];
            ImGui::TextColored(ImVec4(0.16f, 0.50f, 0.96f, 1.0f), "КАРТОЧКА СТУДЕНТА (Семестр %d)", state.currentSemester); ImGui::Separator(); ImGui::Spacing();
            ImGui::Text("ФИО: %s", s.fullName.c_str());
            ImGui::TextDisabled("Группа: %s | Спец: %s", s.group.c_str(), s.specialty.c_str());
            ImGui::Spacing();

            ImGui::TextDisabled("ℹ Подсказка: нажмите правой кнопкой мыши по оценке для её изменения или удаления");
            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

            bool openEditG = false;

            ImGui::BeginChild("GradesList", ImVec2(0, 350), true);

            if (s.semesterGrades.count(state.currentSemester) > 0) {
                for (auto& pair : s.semesterGrades.at(state.currentSemester)) {
                    ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.4f, 1.0f), "%s", pair.first.c_str());

                    float avg = static_cast<float>(s.getSubjectAverage(state.currentSemester, pair.first));
                    char buf[32]; sprintf_s(buf, "(Ср: %.2f)", avg);
                    ImGui::SameLine(ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(buf).x - 10);
                    ImGui::TextDisabled("%s", buf);

                    ImGui::Separator();

                    for (int g = 0; g < (int)pair.second.size(); g++) {
                        ImGui::PushID((pair.first + std::to_string(g)).c_str());

                        if (pair.second[g].type == 2) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.3f, 0.3f, 0.6f));
                        else ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 0.6f));

                        ImGui::Button(pair.second[g].value.c_str(), ImVec2(38, 38));

                        ImGui::PopStyleColor();

                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Дата: %s\nТип: %s",
                                pair.second[g].date.empty() ? "Нет данных" : pair.second[g].date.c_str(),
                                state.config.grade_types[pair.second[g].type].c_str());
                        }

                        bool canEdit = (state.currentUser.role == ROLE_ADMIN) || (state.currentUser.assignedSubject == pair.first);

                        if (canEdit) {
                            if (ImGui::BeginPopupContextItem()) {
                                if (ImGui::MenuItem("✎ Изменить")) {
                                    state.editingSubject = pair.first;
                                    state.editingGradeIdx = g;
                                    state.editingGradeTypeIdx = pair.second[g].type;
                                    strcpy_s(state.editingGradeDate, pair.second[g].date.c_str());

                                    auto it = std::find(state.config.grade_options.begin(), state.config.grade_options.end(), pair.second[g].value);
                                    if (it != state.config.grade_options.end()) {
                                        state.editingGradeValueIdx = (int)std::distance(state.config.grade_options.begin(), it);
                                    }
                                    else {
                                        state.editingGradeValueIdx = 4;
                                    }

                                    openEditG = true;
                                }
                                if (ImGui::MenuItem("✖ Удалить")) {
                                    pair.second.erase(pair.second.begin() + g);
                                    SaveToFile(state);
                                    ImGui::EndPopup(); ImGui::PopID(); break;
                                }
                                ImGui::EndPopup();
                            }
                        }

                        ImGui::SameLine(); ImGui::PopID();
                    }
                    ImGui::NewLine(); ImGui::Spacing();
                }
            }
            ImGui::EndChild();

            if (openEditG) ImGui::OpenPopup("EditGradePopup");

            ImGui::SetNextWindowSizeConstraints(ImVec2(450, -1), ImVec2(600, 600));
            if (ImGui::BeginPopupModal("EditGradePopup", NULL, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
                ImGui::TextColored(ImVec4(0.16f, 0.50f, 0.96f, 1.0f), "ИЗМЕНЕНИЕ ОЦЕНКИ"); ImGui::Separator(); ImGui::Spacing();

                ImGui::TextDisabled("Предмет:"); ImGui::SameLine(); ImGui::Text("%s", state.editingSubject.c_str());
                ImGui::Spacing();

                ImGui::TextDisabled("Дата выставления:"); ImGui::SetNextItemWidth(-1);
                ImGui::InputText("##egdate", state.editingGradeDate, 32);
                ImGui::Spacing();

                ImGui::TextDisabled("Оценка (1-10):"); ImGui::SetNextItemWidth(-1);
                std::string previewText = state.config.grade_options[state.editingGradeValueIdx];
                if (ImGui::BeginCombo("##egr", previewText.c_str())) {
                    for (int i = 0; i < 10; i++) {
                        bool is_selected = (state.editingGradeValueIdx == i);
                        if (ImGui::Selectable(state.config.grade_options[i].c_str(), is_selected)) {
                            state.editingGradeValueIdx = i;
                        }
                        if (is_selected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                ImGui::Spacing();

                ImGui::TextDisabled("Или отметка об отсутствии:");
                float btn_w2 = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2.0f;
                auto DrawEditLetterBtn = [&](int idx, const char* label) {
                    bool selected = (state.editingGradeValueIdx == idx);
                    if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.3f, 0.3f, 1.0f));
                    else ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
                    if (ImGui::Button(label, ImVec2(btn_w2, 36))) state.editingGradeValueIdx = idx;
                    ImGui::PopStyleColor();
                    };
                DrawEditLetterBtn(10, "Н (Неуваж.)"); ImGui::SameLine();
                DrawEditLetterBtn(11, "У (Уваж. / Болезнь)");
                ImGui::Spacing(); ImGui::Spacing();

                ImGui::TextDisabled("Тип работы:"); ImGui::SetNextItemWidth(-1);
                ComboStdString("##egt", &state.editingGradeTypeIdx, state.config.grade_types); ImGui::Spacing();

                ImGui::Separator(); ImGui::Spacing();

                float e_btn_w = static_cast<float>((ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2.0f);
                if (ImGui::Button("СОХРАНИТЬ", ImVec2(e_btn_w, 46))) {
                    auto& gradeRef = s.semesterGrades[state.currentSemester][state.editingSubject][state.editingGradeIdx];
                    gradeRef.value = state.config.grade_options[state.editingGradeValueIdx];
                    gradeRef.type = state.editingGradeTypeIdx;
                    gradeRef.date = state.editingGradeDate;
                    SaveToFile(state);
                    state.AddNotification("Оценка успешно изменена");
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine(); if (ImGui::Button("ОТМЕНА", ImVec2(e_btn_w, 46))) ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }

            if (!s.archiveGrades.empty()) {
                ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.9f, 0.5f, 0.1f, 1.0f), "АРХИВ (Предметы старой специальности)");
                for (auto& archPair : s.archiveGrades) {
                    ImGui::TextDisabled("%s (оценок: %d)", archPair.first.c_str(), (int)archPair.second.size());
                }
            }
        }
        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        if (ImGui::Button("ЗАКРЫТЬ КАРТОЧКУ", ImVec2(-1, 46))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowSizeConstraints(ImVec2(500, -1), ImVec2(800, 800));
    if (ImGui::BeginPopupModal("EditStudentModal", NULL, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
        ImGui::TextColored(ImVec4(0.16f, 0.50f, 0.96f, 1.0f), "РЕДАКТИРОВАНИЕ"); ImGui::Separator(); ImGui::Spacing();
        ImGui::TextDisabled("ФИО:"); ImGui::SetNextItemWidth(-1); ImGui::InputText("##en", state.editName, 128); ImGui::Spacing();
        ImGui::TextDisabled("Группа:"); ImGui::SetNextItemWidth(-1);
        if (ComboStdString("##eg", &state.editGroupIdx, state.config.all_groups)) {
            strcpy_s(state.editSpec, state.config.getSpecialtyName(state.config.all_groups[state.editGroupIdx]).c_str());
        }
        ImGui::Spacing();
        ImGui::TextDisabled("Специальность:"); ImGui::SetNextItemWidth(-1); ImGui::InputText("##es", state.editSpec, 128); ImGui::Spacing();
        ImGui::Separator(); ImGui::Spacing();

        float btn_w = static_cast<float>((ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2.0f);
        if (ImGui::Button("ОБНОВИТЬ", ImVec2(btn_w, 46))) {
            if (state.selectedStudentIdx != -1) {
                std::string oldGroup = state.journal[state.selectedStudentIdx].group;
                std::string newGroup = state.config.all_groups[state.editGroupIdx];

                if (oldGroup != newGroup) {
                    state.transferNewGroup = newGroup;
                    state.transferNewSpec = state.editSpec;
                    state.openTransferConfirm = true;
                    ImGui::CloseCurrentPopup();
                }
                else {
                    state.journal[state.selectedStudentIdx].fullName = state.editName;
                    state.journal[state.selectedStudentIdx].group = newGroup;
                    state.journal[state.selectedStudentIdx].specialty = state.editSpec;
                    SaveToFile(state);
                    state.AddNotification("Данные обновлены");
                    ImGui::CloseCurrentPopup();
                }
            }
        }
        ImGui::SameLine(); if (ImGui::Button("ОТМЕНА", ImVec2(btn_w, 46))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowSizeConstraints(ImVec2(500, -1), ImVec2(800, 800));
    if (ImGui::BeginPopupModal("TransferModal", NULL, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
        ImGui::TextColored(ImVec4(0.9f, 0.5f, 0.1f, 1.0f), "ВНИМАНИЕ! СМЕНА СПЕЦИАЛЬНОСТИ"); ImGui::Separator(); ImGui::Spacing();
        ImGui::TextWrapped("Вы переводите студента в новую группу. Программа автоматически сохранит общие предметы (например, Математика), а старые спец. предметы перенесет в скрытый Архив.");
        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

        float btn_w = static_cast<float>((ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2.0f);
        if (ImGui::Button("ВЫПОЛНИТЬ ПЕРЕВОД", ImVec2(btn_w, 46))) {
            Student& s = state.journal[state.selectedStudentIdx];
            std::vector<std::string> newSubs = state.config.getSubjectsForGroup(state.transferNewGroup);

            for (int sem = 1; sem <= 2; sem++) {
                if (s.semesterGrades.count(sem) == 0) continue;
                auto& subsMap = s.semesterGrades.at(sem);
                auto it = subsMap.begin();
                while (it != subsMap.end()) {
                    std::string subName = it->first;
                    if (std::find(newSubs.begin(), newSubs.end(), subName) == newSubs.end() && subName != "Другое...") {
                        auto& arch = s.archiveGrades[subName];
                        arch.insert(arch.end(), it->second.begin(), it->second.end());
                        it = subsMap.erase(it);
                    }
                    else {
                        it++;
                    }
                }
            }

            s.fullName = state.editName;
            s.group = state.transferNewGroup;
            s.specialty = state.transferNewSpec;
            SaveToFile(state);
            state.AddNotification("Перевод выполнен, предметы очищены", ImVec4(0.9f, 0.5f, 0.1f, 1.0f));
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine(); if (ImGui::Button("ОТМЕНИТЬ", ImVec2(btn_w, 46))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowSizeConstraints(ImVec2(480, -1), ImVec2(600, 600));
    if (ImGui::BeginPopupModal("ConfirmDelete", NULL, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
        ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1.0f), "УДАЛЕНИЕ СТУДЕНТА"); ImGui::Separator(); ImGui::Spacing();
        ImGui::Text("Вы уверены, что хотите удалить эту запись?");
        if (state.studentToDeleteIdx >= 0 && state.studentToDeleteIdx < (int)state.journal.size()) {
            ImGui::Spacing(); ImGui::TextDisabled("ФИО: %s", state.journal[state.studentToDeleteIdx].fullName.c_str());
        }
        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

        float btn_w = static_cast<float>((ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 0.8f));
        if (ImGui::Button("УДАЛИТЬ", ImVec2(btn_w, 46))) {
            if (state.studentToDeleteIdx >= 0) {
                state.journal.erase(state.journal.begin() + state.studentToDeleteIdx);
                SaveToFile(state);
                state.AddNotification("Запись удалена", ImVec4(0.9f, 0.3f, 0.3f, 1.0f));
                state.studentToDeleteIdx = -1;
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor();
        ImGui::SameLine(); if (ImGui::Button("ОТМЕНА", ImVec2(btn_w, 46))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

static void RenderNotifications(AppState& state, float deltaTime) {
    float w_win = ImGui::GetWindowWidth(); float h_win = ImGui::GetWindowHeight(); float y_o = h_win - 85.0f;
    for (auto it = state.notifications.begin(); it != state.notifications.end();) {
        it->timer -= deltaTime;
        if (it->timer <= 0) { it = state.notifications.erase(it); continue; }
        float a = it->timer < 0.5f ? (it->timer / 0.5f) : 1.0f;
        ImGui::SetNextWindowPos(ImVec2(w_win - 340, y_o)); ImGui::SetNextWindowSize(ImVec2(320, 65));
        ImGui::SetNextWindowBgAlpha(a * 0.98f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.12f, 0.12f, 0.15f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Border, it->color); ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.0f);
        ImGui::Begin(("##t" + std::to_string(y_o)).c_str(), NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs);
        ImGui::SetCursorPosY(20); ImGui::SetCursorPosX(15); ImGui::TextColored(it->color, " INFO: %s", it->message.c_str());
        ImGui::End(); ImGui::PopStyleVar(); ImGui::PopStyleColor(2); y_o -= 75.0f; ++it;
    }
}

bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nShowCmd) {
    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(NULL), NULL, NULL, NULL, NULL, L"MTEC_APP", NULL };

    wc.hIcon = LoadIcon(wc.hInstance, MAKEINTRESOURCE(101));   
    wc.hIconSm = LoadIcon(wc.hInstance, MAKEINTRESOURCE(101)); 

    ::RegisterClassExW(&wc);
    RECT rc; GetClientRect(GetDesktopWindow(), &rc);
    HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"МТЭК JOURNAL", WS_POPUP, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, NULL, NULL, wc.hInstance, NULL);

    if (!CreateDeviceD3D(hwnd)) return 1;
    ::ShowWindow(hwnd, SW_SHOWDEFAULT);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();

    io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\Arial.ttf", 20.0f, NULL, io.Fonts->GetGlyphRangesCyrillic());

    ImFontConfig config;
    config.MergeMode = true;
    config.PixelSnapH = true;

    static const ImWchar icon_ranges[] = { 0x2000, 0x27FF, 0 };
    io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\seguisym.ttf", 20.0f, &config, icon_ranges);

    SetupFullScreenModernStyle();
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    AppState state;
    LoadFromFile(state);

    bool done = false;
    auto lastTime = std::chrono::high_resolution_clock::now();

    while (!done) {
        MSG msg;
        while (::PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE)) {
            ::TranslateMessage(&msg); ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT) { SaveToFile(state); done = true; }
        }
        if (done) break;

        auto currentTime = std::chrono::high_resolution_clock::now();
        float deltaTime = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - lastTime).count();
        lastTime = currentTime;

        ImGui_ImplDX11_NewFrame(); ImGui_ImplWin32_NewFrame(); ImGui::NewFrame();

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("Workspace", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoBringToFrontOnFocus);
        ImGui::PopStyleVar();

        if (!state.isLoggedIn) {
            RenderAuthScreen(state, done);
        }
        else {
            RenderSidebar(state, done);
            ImGui::SameLine();

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24, 24));
            ImGui::BeginChild("MainContent", ImVec2(0, 0), true);

            if (state.currentTab == TAB_JOURNAL) RenderJournalTab(state);
            else if (state.currentTab == TAB_STATS) RenderStatsTab(state);
            else if (state.currentTab == TAB_SETTINGS) RenderSettingsTab(state);

            RenderModals(state);
            ImGui::EndChild();
            ImGui::PopStyleVar();
        }

        RenderNotifications(state, deltaTime);

        ImGui::End();
        ImGui::Render();
        const float clr[4] = { 0.06f, 0.06f, 0.08f, 1.0f };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, NULL);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clr);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_pSwapChain->Present(1, 0);
    }

    ImGui_ImplDX11_Shutdown(); ImGui_ImplWin32_Shutdown(); ImGui::DestroyContext(); CleanupDeviceD3D();
    return 0;
}

bool CreateDeviceD3D(HWND hWnd) { DXGI_SWAP_CHAIN_DESC sd; ZeroMemory(&sd, sizeof(sd)); sd.BufferCount = 2; sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; sd.OutputWindow = hWnd; sd.SampleDesc.Count = 1; sd.Windowed = TRUE; sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD; D3D_FEATURE_LEVEL fl; const D3D_FEATURE_LEVEL fla[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 }; if (FAILED(D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, fla, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &fl, &g_pd3dDeviceContext))) return false; CreateRenderTarget(); return true; }
void CreateRenderTarget() { ID3D11Texture2D* p = nullptr; HRESULT hr = g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&p)); if (SUCCEEDED(hr) && p != nullptr) { g_pd3dDevice->CreateRenderTargetView(p, NULL, &g_mainRenderTargetView); p->Release(); } }
void CleanupDeviceD3D() { CleanupRenderTarget(); if (g_pSwapChain) g_pSwapChain->Release(); if (g_pd3dDeviceContext) g_pd3dDeviceContext->Release(); if (g_pd3dDevice) g_pd3dDevice->Release(); }
void CleanupRenderTarget() { if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = NULL; } }
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) { if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return true; switch (msg) { case WM_SIZE: if (g_pd3dDevice != NULL && wParam != SIZE_MINIMIZED) { CleanupRenderTarget(); g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0); CreateRenderTarget(); } return 0; case WM_DESTROY: ::PostQuitMessage(0); return 0; } return ::DefWindowProc(hWnd, msg, wParam, lParam); }