
#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_sdlrenderer2.h"
#include <SDL.h>
#include <stdio.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <sqlite3.h>


const ImVec4 ORANGE = ImVec4(235.0f / 255.0f, 93.0f / 255.0f, 24.0f / 255.0f, 1.0f);
const ImVec4 BLACK = ImVec4(0, 0, 0, 1);
const ImVec4 WHITE = ImVec4(1, 1, 1, 1);
const ImVec4 BG = ImVec4(0.96f, 0.96f, 0.96f, 1);

int selectedSection = 0;
bool showCustomerForm = false;
bool editMode = false;
bool showDeleteConfirmation = false;
int selectedCustomer = -1;

char customerName[128] = "";
char customerPhone[64] = "";
char customerEmail[128] = "";
char customerAddress[256] = "";
char customerSearch[128] = "";

std::string customerMessage;

struct Customer
{
    int id = 0;
    std::string name;
    std::string phone;
    std::string email;
    std::string address;
};


struct Quotation
{
    int id;
    std::string customerName;
    std::string description;
    double amount;
    std::string date;
    std::string status;
};


struct Invoice
{
    int id;
    int quotationId;
    std::string customerName;
    std::string description;
    double amount;
    double paidAmount;
    std::string date;
    std::string status;
};



struct Payment
{
    int id;
    int invoiceId;
    std::string customerName;
    double amount;
    std::string date;
};

std::vector<Payment> payments;
int nextPaymentId = 1;
double paymentAmount = 0.0;
int selectedPaymentInvoice = -1;
std::string paymentMessage;



std::vector<Invoice> invoices;
int nextInvoiceId = 1;
std::string invoiceMessage;

std::vector<Quotation> quotations;

int nextQuotationId = 1;

char quotationDescription[256] = "";
double quotationAmount = 0.0;

std::string quotationMessage;


std::vector<Customer> customers;
int nextCustomerId = 1;

const char* sections[] = {
    "Dashboard",
    "Customers",
    "Quotations",
    "Invoices",
    "Payments",
    "Financial Reports",
    "Settings"
};

std::string lowerText(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });

    return value;
}


std::string businessName = "Your Business Name";
std::string businessPhone = "";
std::string businessEmail = "";
std::string businessAddress = "";

char businessNameInput[128] = "Your Business Name";
char businessPhoneInput[64] = "";
char businessEmailInput[128] = "";
char businessAddressInput[256] = "";

std::string settingsMessage = "";

sqlite3* gDatabase = nullptr;
std::string gDatabaseError;

bool runDatabaseSQL(const char* sql)
{
    char* errorMessage = nullptr;
    int result = sqlite3_exec(gDatabase, sql, nullptr, nullptr, &errorMessage);
    if (result != SQLITE_OK)
    {
        gDatabaseError = errorMessage ? errorMessage : sqlite3_errmsg(gDatabase);
        sqlite3_free(errorMessage);
        return false;
    }
    return true;
}

std::string databaseText(sqlite3_stmt* statement, int column)
{
    const unsigned char* value = sqlite3_column_text(statement, column);
    return value ? reinterpret_cast<const char*>(value) : "";
}

bool initializeDatabase()
{
    gDatabaseError.clear();

    char* prefPath = SDL_GetPrefPath("BizPilot", "BizPilot");

    if (prefPath == nullptr)
    {
        gDatabaseError = std::string("Could not access application data folder: ") + SDL_GetError();
        return false;
    }

    std::string databasePath = std::string(prefPath) + "bizpilot.db";
    SDL_free(prefPath);

    // Preserve the existing database when migrating to AppData.
    {
        std::ifstream existingDatabase(databasePath, std::ios::binary);

        if (!existingDatabase.good())
        {
            std::ifstream oldDatabase("bizpilot.db", std::ios::binary);

            if (oldDatabase.good())
            {
                std::ofstream newDatabase(databasePath, std::ios::binary);

                if (!newDatabase)
                {
                    gDatabaseError = "Could not copy the existing database to AppData.";
                    return false;
                }

                newDatabase << oldDatabase.rdbuf();

                if (!newDatabase.good())
                {
                    gDatabaseError = "Database copy was incomplete.";
                    return false;
                }
            }
        }
    }

    int result = sqlite3_open(databasePath.c_str(), &gDatabase);

    if (result != SQLITE_OK)
    {
        gDatabaseError = gDatabase
            ? sqlite3_errmsg(gDatabase)
            : sqlite3_errstr(result);

        if (gDatabase)
        {
            sqlite3_close(gDatabase);
            gDatabase = nullptr;
        }

        return false;
    }

    sqlite3_busy_timeout(gDatabase, 5000);

    const char* schema = R"SQL(
        PRAGMA journal_mode = WAL;
        PRAGMA foreign_keys = ON;

        CREATE TABLE IF NOT EXISTS customers (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL,
            phone TEXT NOT NULL DEFAULT '',
            email TEXT NOT NULL DEFAULT '',
            address TEXT NOT NULL DEFAULT ''
        );

        CREATE TABLE IF NOT EXISTS quotations (
            id INTEGER PRIMARY KEY,
            customer_name TEXT NOT NULL,
            description TEXT NOT NULL,
            amount REAL NOT NULL,
            date TEXT NOT NULL,
            status TEXT NOT NULL
        );

        CREATE TABLE IF NOT EXISTS invoices (
            id INTEGER PRIMARY KEY,
            quotation_id INTEGER NOT NULL DEFAULT 0,
            customer_name TEXT NOT NULL,
            description TEXT NOT NULL,
            amount REAL NOT NULL,
            paid_amount REAL NOT NULL DEFAULT 0,
            date TEXT NOT NULL,
            status TEXT NOT NULL
        );

        CREATE TABLE IF NOT EXISTS payments (
            id INTEGER PRIMARY KEY,
            invoice_id INTEGER NOT NULL,
            customer_name TEXT NOT NULL,
            amount REAL NOT NULL,
            date TEXT NOT NULL
        );

        CREATE TABLE IF NOT EXISTS business_settings (
            id INTEGER PRIMARY KEY CHECK (id = 1),
            business_name TEXT NOT NULL,
            phone TEXT NOT NULL DEFAULT '',
            email TEXT NOT NULL DEFAULT '',
            address TEXT NOT NULL DEFAULT ''
        );

        CREATE TABLE IF NOT EXISTS app_metadata (
            key TEXT PRIMARY KEY,
            value INTEGER NOT NULL
        );
    )SQL";

    if (!runDatabaseSQL(schema))
    {
        sqlite3_close(gDatabase);
        gDatabase = nullptr;
        return false;
    }

    return true;
}
bool saveDatabase()
{
    if (!gDatabase)
        return false;

    if (!runDatabaseSQL("BEGIN IMMEDIATE;"))
        return false;

    bool ok = runDatabaseSQL(
        "DELETE FROM customers;"
        "DELETE FROM quotations;"
        "DELETE FROM invoices;"
        "DELETE FROM payments;"
    );

    sqlite3_stmt* statement = nullptr;

    auto prepare = [&](const char* sql) -> bool
    {
        if (sqlite3_prepare_v2(gDatabase, sql, -1, &statement, nullptr) != SQLITE_OK)
        {
            gDatabaseError = sqlite3_errmsg(gDatabase);
            return false;
        }
        return true;
    };

    auto bindText = [&](int index, const std::string& value)
    {
        sqlite3_bind_text(statement, index, value.c_str(), -1, SQLITE_TRANSIENT);
    };

    if (ok && prepare("INSERT INTO customers(id, name, phone, email, address) VALUES(?, ?, ?, ?, ?);"))
    {
        for (const auto& customer : customers)
        {
            sqlite3_reset(statement);
            sqlite3_clear_bindings(statement);
            sqlite3_bind_int(statement, 1, customer.id);
            bindText(2, customer.name);
            bindText(3, customer.phone);
            bindText(4, customer.email);
            bindText(5, customer.address);
            if (sqlite3_step(statement) != SQLITE_DONE)
            {
                gDatabaseError = sqlite3_errmsg(gDatabase);
                ok = false;
                break;
            }
        }
        sqlite3_finalize(statement);
        statement = nullptr;
    }
    else if (ok)
        ok = false;

    if (ok && prepare("INSERT INTO quotations(id, customer_name, description, amount, date, status) VALUES(?, ?, ?, ?, ?, ?);"))
    {
        for (const auto& quotation : quotations)
        {
            sqlite3_reset(statement);
            sqlite3_clear_bindings(statement);
            sqlite3_bind_int(statement, 1, quotation.id);
            bindText(2, quotation.customerName);
            bindText(3, quotation.description);
            sqlite3_bind_double(statement, 4, quotation.amount);
            bindText(5, quotation.date);
            bindText(6, quotation.status);
            if (sqlite3_step(statement) != SQLITE_DONE)
            {
                gDatabaseError = sqlite3_errmsg(gDatabase);
                ok = false;
                break;
            }
        }
        sqlite3_finalize(statement);
        statement = nullptr;
    }
    else if (ok)
        ok = false;

    if (ok && prepare("INSERT INTO invoices(id, quotation_id, customer_name, description, amount, paid_amount, date, status) VALUES(?, ?, ?, ?, ?, ?, ?, ?);"))
    {
        for (const auto& invoice : invoices)
        {
            sqlite3_reset(statement);
            sqlite3_clear_bindings(statement);
            sqlite3_bind_int(statement, 1, invoice.id);
            sqlite3_bind_int(statement, 2, invoice.quotationId);
            bindText(3, invoice.customerName);
            bindText(4, invoice.description);
            sqlite3_bind_double(statement, 5, invoice.amount);
            sqlite3_bind_double(statement, 6, invoice.paidAmount);
            bindText(7, invoice.date);
            bindText(8, invoice.status);
            if (sqlite3_step(statement) != SQLITE_DONE)
            {
                gDatabaseError = sqlite3_errmsg(gDatabase);
                ok = false;
                break;
            }
        }
        sqlite3_finalize(statement);
        statement = nullptr;
    }
    else if (ok)
        ok = false;

    if (ok && prepare("INSERT INTO payments(id, invoice_id, customer_name, amount, date) VALUES(?, ?, ?, ?, ?);"))
    {
        for (const auto& payment : payments)
        {
            sqlite3_reset(statement);
            sqlite3_clear_bindings(statement);
            sqlite3_bind_int(statement, 1, payment.id);
            sqlite3_bind_int(statement, 2, payment.invoiceId);
            bindText(3, payment.customerName);
            sqlite3_bind_double(statement, 4, payment.amount);
            bindText(5, payment.date);
            if (sqlite3_step(statement) != SQLITE_DONE)
            {
                gDatabaseError = sqlite3_errmsg(gDatabase);
                ok = false;
                break;
            }
        }
        sqlite3_finalize(statement);
        statement = nullptr;
    }
    else if (ok)
        ok = false;

    if (ok && prepare(
        "INSERT INTO business_settings(id, business_name, phone, email, address) "
        "VALUES(1, ?, ?, ?, ?) "
        "ON CONFLICT(id) DO UPDATE SET business_name=excluded.business_name, "
        "phone=excluded.phone, email=excluded.email, address=excluded.address;"))
    {
        bindText(1, businessName);
        bindText(2, businessPhone);
        bindText(3, businessEmail);
        bindText(4, businessAddress);
        ok = sqlite3_step(statement) == SQLITE_DONE;
        if (!ok)
            gDatabaseError = sqlite3_errmsg(gDatabase);
        sqlite3_finalize(statement);
        statement = nullptr;
    }
    else if (ok)
        ok = false;

    if (ok && prepare(
        "INSERT INTO app_metadata(key, value) VALUES('next_customer_id', ?) "
        "ON CONFLICT(key) DO UPDATE SET value=excluded.value;"))
    {
        sqlite3_bind_int(statement, 1, nextCustomerId);
        ok = sqlite3_step(statement) == SQLITE_DONE;
        if (!ok)
            gDatabaseError = sqlite3_errmsg(gDatabase);
        sqlite3_finalize(statement);
        statement = nullptr;
    }
    else if (ok)
        ok = false;

    if (ok && prepare(
        "INSERT INTO app_metadata(key, value) VALUES('next_quotation_id', ?) "
        "ON CONFLICT(key) DO UPDATE SET value=excluded.value;"))
    {
        sqlite3_bind_int(statement, 1, nextQuotationId);
        ok = sqlite3_step(statement) == SQLITE_DONE;
        if (!ok)
            gDatabaseError = sqlite3_errmsg(gDatabase);
        sqlite3_finalize(statement);
        statement = nullptr;
    }
    else if (ok)
        ok = false;

    if (ok && prepare(
        "INSERT INTO app_metadata(key, value) VALUES('next_invoice_id', ?) "
        "ON CONFLICT(key) DO UPDATE SET value=excluded.value;"))
    {
        sqlite3_bind_int(statement, 1, nextInvoiceId);
        ok = sqlite3_step(statement) == SQLITE_DONE;
        if (!ok)
            gDatabaseError = sqlite3_errmsg(gDatabase);
        sqlite3_finalize(statement);
        statement = nullptr;
    }
    else if (ok)
        ok = false;

    if (ok && prepare(
        "INSERT INTO app_metadata(key, value) VALUES('next_payment_id', ?) "
        "ON CONFLICT(key) DO UPDATE SET value=excluded.value;"))
    {
        sqlite3_bind_int(statement, 1, nextPaymentId);
        ok = sqlite3_step(statement) == SQLITE_DONE;
        if (!ok)
            gDatabaseError = sqlite3_errmsg(gDatabase);
        sqlite3_finalize(statement);
        statement = nullptr;
    }
    else if (ok)
        ok = false;

    if (ok)
    {
        ok = runDatabaseSQL("COMMIT;");
        if (!ok)
            runDatabaseSQL("ROLLBACK;");
    }
    else
    {
        runDatabaseSQL("ROLLBACK;");
    }

    return ok;
}



bool deleteInvoiceFromDatabase(int invoiceId)
{
    if (!gDatabase)
    {
        gDatabaseError = "Database is not open.";
        return false;
    }

    if (invoiceId <= 0)
    {
        gDatabaseError = "Invalid invoice ID.";
        return false;
    }

    // Delete the invoice and its related payment rows as one transaction.
    // If either operation fails, SQLite rolls back and preserves the records.
    if (!runDatabaseSQL("BEGIN IMMEDIATE;"))
        return false;

    sqlite3_stmt* statement = nullptr;

    const char* deletePaymentsSQL =
        "DELETE FROM payments WHERE invoice_id = ?;";

    if (sqlite3_prepare_v2(
            gDatabase,
            deletePaymentsSQL,
            -1,
            &statement,
            nullptr) != SQLITE_OK)
    {
        gDatabaseError = sqlite3_errmsg(gDatabase);
        runDatabaseSQL("ROLLBACK;");
        return false;
    }

    sqlite3_bind_int(statement, 1, invoiceId);
    int result = sqlite3_step(statement);

    if (result != SQLITE_DONE)
    {
        gDatabaseError = sqlite3_errmsg(gDatabase);
        sqlite3_finalize(statement);
        runDatabaseSQL("ROLLBACK;");
        return false;
    }

    sqlite3_finalize(statement);
    statement = nullptr;

    const char* deleteInvoiceSQL =
        "DELETE FROM invoices WHERE id = ?;";

    if (sqlite3_prepare_v2(
            gDatabase,
            deleteInvoiceSQL,
            -1,
            &statement,
            nullptr) != SQLITE_OK)
    {
        gDatabaseError = sqlite3_errmsg(gDatabase);
        runDatabaseSQL("ROLLBACK;");
        return false;
    }

    sqlite3_bind_int(statement, 1, invoiceId);
    result = sqlite3_step(statement);

    if (result != SQLITE_DONE)
    {
        gDatabaseError = sqlite3_errmsg(gDatabase);
        sqlite3_finalize(statement);
        runDatabaseSQL("ROLLBACK;");
        return false;
    }

    const int deletedInvoices = sqlite3_changes(gDatabase);
    sqlite3_finalize(statement);
    statement = nullptr;

    if (deletedInvoices == 0)
    {
        gDatabaseError = "Invoice not found in the database.";
        runDatabaseSQL("ROLLBACK;");
        return false;
    }

    if (!runDatabaseSQL("COMMIT;"))
    {
        runDatabaseSQL("ROLLBACK;");
        return false;
    }

    return true;
}


bool loadDatabase()
{
    if (!gDatabase)
        return false;

    customers.clear();
    quotations.clear();
    invoices.clear();
    payments.clear();

    sqlite3_stmt* statement = nullptr;

    if (sqlite3_prepare_v2(gDatabase,
        "SELECT id, name, phone, email, address FROM customers ORDER BY id;",
        -1, &statement, nullptr) != SQLITE_OK)
    {
        gDatabaseError = sqlite3_errmsg(gDatabase);
        return false;
    }

    while (sqlite3_step(statement) == SQLITE_ROW)
    {
        Customer customer;
        customer.id = sqlite3_column_int(statement, 0);
        customer.name = databaseText(statement, 1);
        customer.phone = databaseText(statement, 2);
        customer.email = databaseText(statement, 3);
        customer.address = databaseText(statement, 4);
        nextCustomerId = std::max(nextCustomerId, customer.id + 1);
        customers.push_back(customer);
    }
    sqlite3_finalize(statement);
    statement = nullptr;

    if (sqlite3_prepare_v2(gDatabase,
        "SELECT id, customer_name, description, amount, date, status FROM quotations ORDER BY id;",
        -1, &statement, nullptr) != SQLITE_OK)
    {
        gDatabaseError = sqlite3_errmsg(gDatabase);
        return false;
    }

    int maxQuotationId = 0;
    while (sqlite3_step(statement) == SQLITE_ROW)
    {
        Quotation quotation;
        quotation.id = sqlite3_column_int(statement, 0);
        quotation.customerName = databaseText(statement, 1);
        quotation.description = databaseText(statement, 2);
        quotation.amount = sqlite3_column_double(statement, 3);
        quotation.date = databaseText(statement, 4);
        quotation.status = databaseText(statement, 5);
        maxQuotationId = std::max(maxQuotationId, quotation.id);
        quotations.push_back(quotation);
    }
    sqlite3_finalize(statement);
    statement = nullptr;

    if (sqlite3_prepare_v2(gDatabase,
        "SELECT id, quotation_id, customer_name, description, amount, paid_amount, date, status FROM invoices ORDER BY id;",
        -1, &statement, nullptr) != SQLITE_OK)
    {
        gDatabaseError = sqlite3_errmsg(gDatabase);
        return false;
    }

    int maxInvoiceId = 0;
    while (sqlite3_step(statement) == SQLITE_ROW)
    {
        Invoice invoice;
        invoice.id = sqlite3_column_int(statement, 0);
        invoice.quotationId = sqlite3_column_int(statement, 1);
        invoice.customerName = databaseText(statement, 2);
        invoice.description = databaseText(statement, 3);
        invoice.amount = sqlite3_column_double(statement, 4);
        invoice.paidAmount = sqlite3_column_double(statement, 5);
        invoice.date = databaseText(statement, 6);
        invoice.status = databaseText(statement, 7);
        maxInvoiceId = std::max(maxInvoiceId, invoice.id);
        invoices.push_back(invoice);
    }
    sqlite3_finalize(statement);
    statement = nullptr;

    if (sqlite3_prepare_v2(gDatabase,
        "SELECT id, invoice_id, customer_name, amount, date FROM payments ORDER BY id;",
        -1, &statement, nullptr) != SQLITE_OK)
    {
        gDatabaseError = sqlite3_errmsg(gDatabase);
        return false;
    }

    int maxPaymentId = 0;
    while (sqlite3_step(statement) == SQLITE_ROW)
    {
        Payment payment;
        payment.id = sqlite3_column_int(statement, 0);
        payment.invoiceId = sqlite3_column_int(statement, 1);
        payment.customerName = databaseText(statement, 2);
        payment.amount = sqlite3_column_double(statement, 3);
        payment.date = databaseText(statement, 4);
        maxPaymentId = std::max(maxPaymentId, payment.id);
        payments.push_back(payment);
    }
    sqlite3_finalize(statement);
    statement = nullptr;

    nextQuotationId = maxQuotationId + 1;
    nextInvoiceId = maxInvoiceId + 1;
    nextPaymentId = maxPaymentId + 1;

    if (sqlite3_prepare_v2(gDatabase,
        "SELECT key, value FROM app_metadata;",
        -1, &statement, nullptr) == SQLITE_OK)
    {
        while (sqlite3_step(statement) == SQLITE_ROW)
        {
            std::string key = databaseText(statement, 0);
            int value = sqlite3_column_int(statement, 1);
            if (key == "next_customer_id")
                nextCustomerId = std::max(nextCustomerId, value);
            else if (key == "next_quotation_id")
                nextQuotationId = std::max(nextQuotationId, value);
            else if (key == "next_invoice_id")
                nextInvoiceId = std::max(nextInvoiceId, value);
            else if (key == "next_payment_id")
                nextPaymentId = std::max(nextPaymentId, value);
        }
        sqlite3_finalize(statement);
        statement = nullptr;
    }

    if (sqlite3_prepare_v2(gDatabase,
        "SELECT business_name, phone, email, address FROM business_settings WHERE id=1;",
        -1, &statement, nullptr) != SQLITE_OK)
    {
        gDatabaseError = sqlite3_errmsg(gDatabase);
        return false;
    }

    if (sqlite3_step(statement) == SQLITE_ROW)
    {
        businessName = databaseText(statement, 0);
        businessPhone = databaseText(statement, 1);
        businessEmail = databaseText(statement, 2);
        businessAddress = databaseText(statement, 3);
    }
    else
    {
        std::ifstream settingsFile("business_settings.txt");
        std::string name, phone, email, address;
        if (settingsFile &&
            std::getline(settingsFile, name) &&
            std::getline(settingsFile, phone) &&
            std::getline(settingsFile, email) &&
            std::getline(settingsFile, address) &&
            !name.empty())
        {
            businessName = name;
            businessPhone = phone;
            businessEmail = email;
            businessAddress = address;
        }
    }
    sqlite3_finalize(statement);

    return saveDatabase();
}




void applyModernTheme()
{
    ImGui::StyleColorsLight();

    ImGuiStyle& style = ImGui::GetStyle();

    style.WindowPadding = ImVec2(20, 20);
    style.FramePadding = ImVec2(12, 9);
    style.CellPadding = ImVec2(10, 8);
    style.ItemSpacing = ImVec2(10, 12);
    style.ItemInnerSpacing = ImVec2(8, 6);
    style.WindowRounding = 0;
    style.ChildRounding = 10;
    style.FrameRounding = 7;
    style.PopupRounding = 7;
    style.ScrollbarRounding = 8;
    style.GrabRounding = 7;
    style.WindowBorderSize = 0;
    style.ChildBorderSize = 0;
    style.FrameBorderSize = 0;

    ImVec4* colors = style.Colors;

    colors[ImGuiCol_WindowBg] = BG;
    colors[ImGuiCol_ChildBg] = BG;
    colors[ImGuiCol_PopupBg] = WHITE;
    colors[ImGuiCol_Text] = BLACK;
    colors[ImGuiCol_TextDisabled] = ImVec4(0.40f, 0.40f, 0.40f, 1);
    colors[ImGuiCol_Border] = ImVec4(0.88f, 0.88f, 0.88f, 1);

    colors[ImGuiCol_FrameBg] = WHITE;
    colors[ImGuiCol_FrameBgHovered] = ImVec4(1, 0.91f, 0.86f, 1);
    colors[ImGuiCol_FrameBgActive] = ImVec4(1, 0.84f, 0.75f, 1);

    colors[ImGuiCol_Button] = ORANGE;
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.98f, 0.43f, 0.20f, 1);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.80f, 0.28f, 0.06f, 1);

    colors[ImGuiCol_Header] = ImVec4(1, 0.88f, 0.81f, 1);
    colors[ImGuiCol_HeaderHovered] = ImVec4(1, 0.80f, 0.70f, 1);
    colors[ImGuiCol_HeaderActive] = ORANGE;

    colors[ImGuiCol_CheckMark] = ORANGE;
    colors[ImGuiCol_SliderGrab] = ORANGE;
    colors[ImGuiCol_SliderGrabActive] = ORANGE;
    colors[ImGuiCol_Separator] = ImVec4(0.86f, 0.86f, 0.86f, 1);
}

void drawSidebar(float sidebarWidth)
{
    ImGui::PushStyleColor(ImGuiCol_ChildBg, BLACK);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18, 22));

    ImGui::BeginChild("Sidebar", ImVec2(sidebarWidth, 0), false);

    ImGui::Dummy(ImVec2(0, 8));

    ImGui::PushStyleColor(ImGuiCol_Text, WHITE);
    ImGui::SetWindowFontScale(1.15f);
    ImGui::Text("           BizPilot");
    ImGui::SetWindowFontScale(1);
    ImGui::PopStyleColor();

    ImGui::Dummy(ImVec2(0, 18));
    ImGui::TextDisabled("            MAIN MENU");
    ImGui::Dummy(ImVec2(0, 10));

    for (int i = 0; i < IM_ARRAYSIZE(sections); i++)
    {
        bool selected = selectedSection == i;

        ImGui::PushStyleColor(
            ImGuiCol_Button,
            selected ? ORANGE : ImVec4(0.10f, 0.10f, 0.10f, 1)
        );

        ImGui::PushStyleColor(
            ImGuiCol_ButtonHovered,
            selected ? ImVec4(0.98f, 0.43f, 0.20f, 1)
                     : ImVec4(0.22f, 0.22f, 0.22f, 1)
        );

        ImGui::PushStyleColor(
            ImGuiCol_ButtonActive,
            ImVec4(0.30f, 0.30f, 0.30f, 1)
        );

        ImGui::PushStyleColor(ImGuiCol_Text, WHITE);

        if (ImGui::Button(sections[i], ImVec2(-1, 42)))
            selectedSection = i;

        ImGui::PopStyleColor(4);
        ImGui::Dummy(ImVec2(0, 3));
    }

    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

void drawSummaryCard(
    const char* title,
    const char* value,
    const char* description)
{
    ImGui::PushStyleVar(
        ImGuiStyleVar_WindowPadding,
        ImVec2(18, 16)
    );

    ImGui::PushStyleColor(ImGuiCol_ChildBg, WHITE);
    ImGui::BeginChild(title, ImVec2(0, 115), true);

    ImGui::TextDisabled("%s", title);

    ImGui::PushStyleColor(ImGuiCol_Text, ORANGE);
    ImGui::SetWindowFontScale(1.35f);
    ImGui::Text("%s", value);
    ImGui::SetWindowFontScale(1);
    ImGui::PopStyleColor();

    ImGui::TextDisabled("%s", description);

    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}


void drawStatCard(const char* title, const char* value,
                  const char* description, int targetSection)
{
    ImGui::PushID(title);

    ImGui::PushStyleColor(
        ImGuiCol_Button,
        ImVec4(0.12f, 0.12f, 0.12f, 1.0f)
    );
    ImGui::PushStyleColor(
        ImGuiCol_ButtonHovered,
        ImVec4(0.20f, 0.18f, 0.16f, 1.0f)
    );
    ImGui::PushStyleColor(
        ImGuiCol_ButtonActive,
        ImVec4(0.28f, 0.20f, 0.15f, 1.0f)
    );

    ImGui::PushStyleVar(
        ImGuiStyleVar_FrameRounding, 10.0f
    );

    ImGui::BeginGroup();

    if (ImGui::Button("##card", ImVec2(-1, 125)))
        selectedSection = targetSection;

    ImVec2 pos = ImGui::GetItemRectMin();
    ImVec2 size = ImGui::GetItemRectSize();

    ImDrawList* drawList = ImGui::GetWindowDrawList();

    drawList->AddRectFilled(
        ImVec2(pos.x, pos.y),
        ImVec2(pos.x + 4, pos.y + size.y),
        IM_COL32(235, 93, 24, 255),
        4.0f
    );

    ImGui::SetCursorScreenPos(
        ImVec2(pos.x + 16, pos.y + 14)
    );
    ImGui::TextDisabled("%s", title);

    ImGui::SetCursorScreenPos(
        ImVec2(pos.x + 16, pos.y + 42)
    );
    ImGui::SetWindowFontScale(1.35f);
    ImGui::TextColored(
        ImVec4(0.92f, 0.36f, 0.09f, 1.0f),
        "%s", value
    );
    ImGui::SetWindowFontScale(1.0f);

    ImGui::SetCursorScreenPos(
        ImVec2(pos.x + 16, pos.y + 91)
    );
    ImGui::TextDisabled("%s", description);

    ImGui::EndGroup();

    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
    ImGui::PopID();
}



void drawPieChart(const char* title, double firstValue, double secondValue,
                  const char* firstLabel, const char* secondLabel)
{
    ImGui::BeginGroup();

    ImGui::Text("%s", title);
    ImGui::Spacing();

    ImVec2 available = ImGui::GetContentRegionAvail();

    float chartSize = 141.0f;
    float panelWidth = available.x;

    if (panelWidth < chartSize)
        chartSize = panelWidth;

    ImVec2 cursor = ImGui::GetCursorScreenPos();

    ImVec2 center(
        cursor.x + panelWidth * 0.5f,
        cursor.y + chartSize * 0.5f + 4.0f
    );

    float radius = chartSize * 0.43f;
    const int segments = 64;
    const float PI = 3.14159265358979323846f;

    double total = firstValue + secondValue;

    float firstFraction = total > 0.0
        ? static_cast<float>(firstValue / total)
        : 0.0f;

    ImDrawList* drawList = ImGui::GetWindowDrawList();

    ImVec2 mouse = ImGui::GetIO().MousePos;
    float dx = mouse.x - center.x;
    float dy = mouse.y - center.y;

    bool hovered =
        (dx * dx + dy * dy) <= radius * radius;

    ImU32 orange = hovered
        ? IM_COL32(255, 125, 45, 255)
        : IM_COL32(235, 93, 24, 255);

    ImU32 black = hovered
        ? IM_COL32(65, 65, 65, 255)
        : IM_COL32(25, 25, 25, 255);

    drawList->AddCircleFilled(
        center, radius, black, segments
    );

    if (firstFraction > 0.0f)
    {
        float endAngle = -PI / 2.0f
                       + firstFraction * 2.0f * PI;

        drawList->PathClear();
        drawList->PathLineTo(center);
        drawList->PathArcTo(
            center, radius, -PI / 2.0f, endAngle, segments
        );
        drawList->PathLineTo(center);
        drawList->PathFillConvex(orange);
    }

    drawList->AddCircle(
        center, radius,
        hovered ? IM_COL32(255, 125, 45, 255)
                : IM_COL32(0, 0, 0, 255),
        segments, hovered ? 3.0f : 2.0f
    );

    ImGui::Dummy(ImVec2(panelWidth, chartSize + 12.0f));

    ImGui::Spacing();

    ImGui::TextColored(
        ImVec4(0.92f, 0.36f, 0.09f, 1.0f),
        "%s: %.2f", firstLabel, firstValue
    );

    ImGui::Text(
        "%s: %.2f", secondLabel, secondValue
    );

    ImGui::EndGroup();
}


void drawDashboard()
{
    double totalInvoiced = 0.0;
    double totalPaid = 0.0;
    double outstanding = 0.0;

    int paidInvoices = 0;
    int unpaidInvoices = 0;

    double dailyRevenue = 0.0;
    double weeklyRevenue = 0.0;
    double monthlyRevenue = 0.0;
    double yearlyRevenue = 0.0;

    std::time_t now = std::time(nullptr);
    std::tm today = *std::localtime(&now);

    int currentYear = today.tm_year + 1900;
    int currentMonth = today.tm_mon + 1;
    int currentDay = today.tm_mday;

    std::tm todayStart = today;
    todayStart.tm_hour = 0;
    todayStart.tm_min = 0;
    todayStart.tm_sec = 0;

    std::time_t todayTime = std::mktime(&todayStart);
    std::time_t weekStart = todayTime - (6 * 24 * 60 * 60);

    for (const auto& invoice : invoices)
    {
        totalInvoiced += invoice.amount;

        double paid = invoice.paidAmount;
        if (paid < 0.0)
            paid = 0.0;

        if (paid > invoice.amount)
            paid = invoice.amount;

        totalPaid += paid;

        double balance = invoice.amount - paid;
        if (balance < 0.0)
            balance = 0.0;

        outstanding += balance;

        if (balance <= 0.001)
            paidInvoices++;
        else
            unpaidInvoices++;

        int year = 0;
        int month = 0;
        int day = 0;

        if (std::sscanf(
                invoice.date.c_str(),
                "%d-%d-%d",
                &year, &month, &day) == 3)
        {
            if (month >= 1 && month <= 12 &&
                day >= 1 && day <= 31)
            {
                if (year == currentYear &&
                    month == currentMonth &&
                    day == currentDay)
                {
                    dailyRevenue += invoice.amount;
                }

                if (year == currentYear &&
                    month == currentMonth)
                {
                    monthlyRevenue += invoice.amount;
                }

                if (year == currentYear)
                {
                    yearlyRevenue += invoice.amount;
                }

                std::tm invoiceDate = {};
                invoiceDate.tm_year = year - 1900;
                invoiceDate.tm_mon = month - 1;
                invoiceDate.tm_mday = day;
                invoiceDate.tm_isdst = -1;

                std::time_t invoiceTime =
                    std::mktime(&invoiceDate);

                if (invoiceTime >= weekStart &&
                    invoiceTime <= now)
                {
                    weeklyRevenue += invoice.amount;
                }
            }
        }
    }

    ImGui::Text("Dashboard");

    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("Revenue Overview");
    ImGui::Spacing();

    if (ImGui::BeginTable(
            "RevenuePeriods",
            4,
            ImGuiTableFlags_SizingStretchSame))
    {
        ImGui::TableNextColumn();
        ImGui::TextDisabled("Today");
        ImGui::TextColored(
            ImVec4(0.92f, 0.36f, 0.09f, 1.0f),
            "%.2f", dailyRevenue
        );

        ImGui::TableNextColumn();
        ImGui::TextDisabled("Last 7 Days");
        ImGui::TextColored(
            ImVec4(0.92f, 0.36f, 0.09f, 1.0f),
            "%.2f", weeklyRevenue
        );

        ImGui::TableNextColumn();
        ImGui::TextDisabled("This Month");
        ImGui::TextColored(
            ImVec4(0.92f, 0.36f, 0.09f, 1.0f),
            "%.2f", monthlyRevenue
        );

        ImGui::TableNextColumn();
        ImGui::TextDisabled("This Year");
        ImGui::TextColored(
            ImVec4(0.92f, 0.36f, 0.09f, 1.0f),
            "%.2f", yearlyRevenue
        );

        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("Financial Pie Charts");
    ImGui::TextDisabled(
        "Paid and outstanding amounts, and invoice status"
    );
    ImGui::Spacing();

    if (ImGui::BeginTable(
            "DashboardPieCharts",
            2,
            ImGuiTableFlags_SizingStretchSame))
    {
        ImGui::TableNextColumn();

        ImGui::BeginChild(
            "PaidOutstandingChart",
            ImVec2(0, 255),
            true
        );

        drawPieChart(
            "Payment Status",
            totalPaid,
            outstanding,
            "Paid",
            "Outstanding"
        );

        ImGui::EndChild();

        ImGui::TableNextColumn();

        ImGui::BeginChild(
            "InvoiceStatusChart",
            ImVec2(0, 255),
            true
        );

        drawPieChart(
            "Invoice Status",
            static_cast<double>(paidInvoices),
            static_cast<double>(unpaidInvoices),
            "Paid Invoices",
            "Unpaid Invoices"
        );

        ImGui::EndChild();

        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    char customerCount[32];
    char quotationCount[32];
    char invoiceCount[32];
    char paymentTotal[64];
    char outstandingTotal[64];
    char unpaidCount[32];

    std::snprintf(
        customerCount, sizeof(customerCount),
        "%d", static_cast<int>(customers.size())
    );

    std::snprintf(
        quotationCount, sizeof(quotationCount),
        "%d", static_cast<int>(quotations.size())
    );

    std::snprintf(
        invoiceCount, sizeof(invoiceCount),
        "%d", static_cast<int>(invoices.size())
    );

    std::snprintf(
        paymentTotal, sizeof(paymentTotal),
        "%.2f", totalPaid
    );

    std::snprintf(
        outstandingTotal, sizeof(outstandingTotal),
        "%.2f", outstanding
    );

    std::snprintf(
        unpaidCount, sizeof(unpaidCount),
        "%d", unpaidInvoices
    );

    ImGui::Text("Business Overview");
    ImGui::Spacing();

    if (ImGui::BeginTable(
            "InteractiveCards",
            3,
            ImGuiTableFlags_SizingStretchSame))
    {
        ImGui::TableNextColumn();
        drawStatCard(
            "Customers", customerCount,
            "Total customers", 1
        );

        ImGui::TableNextColumn();
        drawStatCard(
            "Quotations", quotationCount,
            "Total quotations", 2
        );

        ImGui::TableNextColumn();
        drawStatCard(
            "Invoices", invoiceCount,
            "Total invoices", 3
        );

        ImGui::TableNextRow();

        ImGui::TableNextColumn();
        drawStatCard(
            "Payments Received", paymentTotal,
            "Paid invoice amounts", 4
        );

        ImGui::TableNextColumn();
        drawStatCard(
            "Outstanding", outstandingTotal,
            "Remaining balance", 3
        );

        ImGui::TableNextColumn();
        drawStatCard(
            "Unpaid Invoices", unpaidCount,
            "Unpaid or partial", 3
        );

        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("Recent Activity");
    ImGui::TextDisabled("Latest invoices and payments");
    ImGui::Spacing();

    if (ImGui::BeginTable(
            "RecentActivityTable",
            5,
            ImGuiTableFlags_Borders |
            ImGuiTableFlags_RowBg |
            ImGuiTableFlags_SizingStretchSame))
    {
        ImGui::TableSetupColumn("Type");
        ImGui::TableSetupColumn("Customer");
        ImGui::TableSetupColumn("Reference");
        ImGui::TableSetupColumn("Amount");
        ImGui::TableSetupColumn("Date");
        ImGui::TableHeadersRow();

        struct ActivityItem
        {
            std::string type;
            std::string customer;
            std::string reference;
            double amount;
            std::string date;
        };

        std::vector<ActivityItem> activities;

        for (const auto& invoice : invoices)
        {
            activities.push_back({
                "Invoice",
                invoice.customerName,
                "INV-" + std::to_string(invoice.id),
                invoice.amount,
                invoice.date
            });
        }

        for (const auto& payment : payments)
        {
            activities.push_back({
                "Payment",
                payment.customerName,
                "INV-" + std::to_string(payment.invoiceId),
                payment.amount,
                payment.date
            });
        }

        std::sort(
            activities.begin(),
            activities.end(),
            [](const ActivityItem& a, const ActivityItem& b)
            {
                return a.date > b.date;
            }
        );

        int displayed = 0;

        for (const auto& activity : activities)
        {
            if (displayed >= 8)
                break;

            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            ImGui::TextColored(
                ImVec4(0.92f, 0.36f, 0.09f, 1.0f),
                "%s", activity.type.c_str()
            );

            ImGui::TableNextColumn();
            ImGui::Text(
                "%s", activity.customer.c_str()
            );

            ImGui::TableNextColumn();
            ImGui::Text(
                "%s", activity.reference.c_str()
            );

            ImGui::TableNextColumn();
            ImGui::Text(
                "%.2f", activity.amount
            );

            ImGui::TableNextColumn();
            ImGui::Text(
                "%s", activity.date.c_str()
            );

            displayed++;
        }

        if (displayed == 0)
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextDisabled("No recent activity yet.");
        }

        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::Dummy(ImVec2(0, 30));
}

void clearCustomerForm()
{
    customerName[0] = '\0';
    customerPhone[0] = '\0';
    customerEmail[0] = '\0';
    customerAddress[0] = '\0';
}

void copyCustomerToForm(const Customer& customer)
{
    snprintf(customerName, sizeof(customerName), "%s",
        customer.name.c_str());

    snprintf(customerPhone, sizeof(customerPhone), "%s",
        customer.phone.c_str());

    snprintf(customerEmail, sizeof(customerEmail), "%s",
        customer.email.c_str());

    snprintf(customerAddress, sizeof(customerAddress), "%s",
        customer.address.c_str());
}

void drawCustomerForm()
{
    ImGui::PushStyleColor(ImGuiCol_ChildBg, WHITE);
    ImGui::BeginChild("CustomerForm", ImVec2(0, 0), true);

    ImGui::Text("%s", editMode ? "Edit Customer" : "New Customer");
    ImGui::TextDisabled("Enter the customer's contact information.");
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 8));

    ImGui::Text("Full Name *");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##CustomerName", customerName,
        sizeof(customerName));

    ImGui::Text("Phone Number");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##CustomerPhone", customerPhone,
        sizeof(customerPhone));

    ImGui::Text("Email Address");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##CustomerEmail", customerEmail,
        sizeof(customerEmail));

    ImGui::Text("Address");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##CustomerAddress", customerAddress,
        sizeof(customerAddress));

    ImGui::Dummy(ImVec2(0, 12));

    if (ImGui::Button(
        editMode ? "Save Changes" : "Save Customer",
        ImVec2(150, 42)))
    {
        if (std::string(customerName).find_first_not_of(" \t\n")
            == std::string::npos)
        {
            customerMessage = "Please enter the customer's name.";
        }
        else if (editMode &&
                 selectedCustomer >= 0 &&
                 selectedCustomer < static_cast<int>(customers.size()))
        {
            customers[selectedCustomer].name = customerName;
            customers[selectedCustomer].phone = customerPhone;
            customers[selectedCustomer].email = customerEmail;
            customers[selectedCustomer].address = customerAddress;

            if (!saveDatabase())
                customerMessage = "Database save failed: " + gDatabaseError;
            else
                customerMessage = "Customer updated successfully.";
            showCustomerForm = false;
            editMode = false;
            selectedCustomer = -1;
            clearCustomerForm();
        }
        else if (!editMode)
        {
            Customer customer;
            customer.id = nextCustomerId++;
            customer.name = customerName;
            customer.phone = customerPhone;
            customer.email = customerEmail;
            customer.address = customerAddress;

            customers.push_back(customer);

            if (!saveDatabase())
                customerMessage = "Database save failed: " + gDatabaseError;
            else
                customerMessage = "Customer added successfully.";
            showCustomerForm = false;
            clearCustomerForm();
        }
    }

    ImGui::SameLine();

    if (ImGui::Button("Cancel", ImVec2(110, 42)))
    {
        clearCustomerForm();
        showCustomerForm = false;
        editMode = false;
        selectedCustomer = -1;
        customerMessage.clear();
    }

    if (!customerMessage.empty())
    {
        ImGui::Dummy(ImVec2(0, 8));
        ImGui::TextWrapped("%s", customerMessage.c_str());
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
}


void deleteCustomer(int index)
{
    if (index >= 0 && index < static_cast<int>(customers.size()))
    {
        customers.erase(customers.begin() + index);
        if (!saveDatabase())
            customerMessage = "Database save failed: " + gDatabaseError;
        else
            customerMessage = "Customer deleted successfully.";
    }
}


void drawCustomers()
{
    ImGui::Text("Customers");
    ImGui::TextDisabled("Manage your customer contact details.");
    ImGui::Dummy(ImVec2(0, 12));

    if (ImGui::Button("+ Add Customer", ImVec2(160, 42)))
    {
        clearCustomerForm();
        showCustomerForm = true;
        editMode = false;
        selectedCustomer = -1;
        customerMessage.clear();
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(260);

    ImGui::InputTextWithHint(
        "##CustomerSearch",
        "Search name, phone, or email...",
        customerSearch,
        sizeof(customerSearch)
    );

    ImGui::SameLine();
    ImGui::Text("Total: %d", static_cast<int>(customers.size()));

    ImGui::Dummy(ImVec2(0, 12));

    if (showCustomerForm)
    {
        drawCustomerForm();
        return;
    }

    static int pendingDelete = -1;

    ImGui::PushStyleColor(ImGuiCol_ChildBg, WHITE);
    ImGui::BeginChild("CustomerTablePanel", ImVec2(0, 0), true);

    int deleteRequested = -1;

    if (ImGui::BeginTable(
        "CustomerTable",
        5,
        ImGuiTableFlags_BordersInnerV |
        ImGuiTableFlags_BordersOuter |
        ImGuiTableFlags_RowBg |
        ImGuiTableFlags_Resizable |
        ImGuiTableFlags_ScrollY))
    {
        ImGui::TableSetupColumn("Name");
        ImGui::TableSetupColumn("Phone");
        ImGui::TableSetupColumn("Email");
        ImGui::TableSetupColumn("Address");
        ImGui::TableSetupColumn(
            "Actions",
            ImGuiTableColumnFlags_WidthFixed,
            145
        );

        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        std::string search = lowerText(customerSearch);

        for (int i = 0; i < static_cast<int>(customers.size()); i++)
        {
            const Customer& customer = customers[i];

            std::string combined = lowerText(
                customer.name + " " +
                customer.phone + " " +
                customer.email + " " +
                customer.address
            );

            if (!search.empty() &&
                combined.find(search) == std::string::npos)
            {
                continue;
            }

            ImGui::PushID(i);
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::TextWrapped("%s", customer.name.c_str());

            ImGui::TableSetColumnIndex(1);
            ImGui::TextWrapped("%s", customer.phone.c_str());

            ImGui::TableSetColumnIndex(2);
            ImGui::TextWrapped("%s", customer.email.c_str());

            ImGui::TableSetColumnIndex(3);
            ImGui::TextWrapped("%s", customer.address.c_str());

            ImGui::TableSetColumnIndex(4);

            if (ImGui::SmallButton("Edit"))
            {
                selectedCustomer = i;
                copyCustomerToForm(customers[i]);
                editMode = true;
                showCustomerForm = true;
                customerMessage.clear();
            }

            ImGui::SameLine();

            if (ImGui::SmallButton("Delete"))
            {
                deleteRequested = i;
            }

            ImGui::PopID();
        }

        ImGui::EndTable();
    }

    if (customers.empty())
    {
        ImGui::Dummy(ImVec2(0, 14));
        ImGui::TextDisabled(
            "No customers yet. Select '+ Add Customer' to begin."
        );
    }

    if (!customerMessage.empty())
    {
        ImGui::Dummy(ImVec2(0, 8));
        ImGui::TextWrapped("%s", customerMessage.c_str());
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();

    // Open the popup after the table and child window have finished.
    if (deleteRequested >= 0)
    {
        pendingDelete = deleteRequested;
        ImGui::OpenPopup("Confirm Delete");
    }

    // Draw the popup every frame so it remains visible.

if (ImGui::BeginPopupModal(
        "Confirm Delete",
        nullptr,
        ImGuiWindowFlags_AlwaysAutoResize))
{
    ImGui::Text("Are you sure you want to delete this customer?");
    ImGui::TextDisabled("This action cannot be undone.");
    ImGui::Dummy(ImVec2(0, 10));

    // Both buttons have equal dimensions.
    if (ImGui::Button("Yes, Delete", ImVec2(120, 40)))
    {
        if (pendingDelete >= 0 &&
            pendingDelete < static_cast<int>(customers.size()))
        {
            Customer deletedCustomer = customers[pendingDelete];
            customers.erase(customers.begin() + pendingDelete);

            if (!saveDatabase())
            {
                customers.insert(
                    customers.begin() + pendingDelete,
                    deletedCustomer);

                customerMessage =
                    "Database save failed: " + gDatabaseError;
            }
            else
            {
                customerMessage = "Customer deleted successfully.";
            }
        }

        pendingDelete = -1;
        ImGui::CloseCurrentPopup();
    }

    ImGui::SameLine();

    if (ImGui::Button("Cancel", ImVec2(120, 40)))
    {
        pendingDelete = -1;
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

}


void drawQuotations()
{
    ImGui::Text("Quotations");
    ImGui::TextDisabled("Create and manage customer quotations.");
    ImGui::Dummy(ImVec2(0, 12));

    if (customers.empty())
    {
        ImGui::TextWrapped(
            "Add a customer on the Customers page before creating a quotation."
        );
        return;
    }

    static int selectedQuotationCustomer = 0;
    static int editingQuotationId = -1;
    static int pendingDeleteQuotationId = -1;
    static bool showDeletePopup = false;

    if (selectedQuotationCustomer >= static_cast<int>(customers.size()))
        selectedQuotationCustomer = 0;

    ImGui::Text(editingQuotationId == -1
        ? "Create New Quotation"
        : "Edit Quotation");

    ImGui::Text("Customer");

    std::vector<const char*> customerNames;
    for (const auto& customer : customers)
        customerNames.push_back(customer.name.c_str());

    ImGui::SetNextItemWidth(300);
    ImGui::Combo(
        "##QuotationCustomer",
        &selectedQuotationCustomer,
        customerNames.data(),
        static_cast<int>(customerNames.size())
    );

    ImGui::Text("Description");
    ImGui::SetNextItemWidth(400);
    ImGui::InputText(
        "##QuotationDescription",
        quotationDescription,
        sizeof(quotationDescription)
    );

    ImGui::Text("Amount");
    ImGui::SetNextItemWidth(200);
    ImGui::InputDouble(
        "##QuotationAmount",
        &quotationAmount,
        0.0,
        0.0,
        "%.2f"
    );

    ImGui::Dummy(ImVec2(0, 8));

    if (editingQuotationId == -1)
    {
        if (ImGui::Button("Create Quotation", ImVec2(180, 42)))
        {
            if (quotationDescription[0] == '\0')
                quotationMessage = "Please enter a description.";
            else if (quotationAmount <= 0.0)
                quotationMessage = "Amount must be greater than zero.";
            else
            {
                Quotation quotation;
                quotation.id = nextQuotationId++;
                quotation.customerName =
                    customers[selectedQuotationCustomer].name;
                quotation.description = quotationDescription;
                quotation.amount = quotationAmount;
                quotation.date = "Today";
                quotation.status = "Pending";

                quotations.push_back(quotation);

                if (!saveDatabase())
                    quotationMessage = "Database save failed: " + gDatabaseError;

                quotationDescription[0] = '\0';
                quotationAmount = 0.0;
                quotationMessage = "Quotation created successfully.";
            }
        }
    }
    else
    {
        if (ImGui::Button("Save Changes", ImVec2(150, 42)))
        {
            if (quotationDescription[0] == '\0')
                quotationMessage = "Please enter a description.";
            else if (quotationAmount <= 0.0)
                quotationMessage = "Amount must be greater than zero.";
            else
            {
                for (auto& quotation : quotations)
                {
                    if (quotation.id == editingQuotationId)
                    {
                        quotation.customerName =
                            customers[selectedQuotationCustomer].name;
                        quotation.description = quotationDescription;
                        quotation.amount = quotationAmount;
                        break;
                    }
                }

                editingQuotationId = -1;
                quotationDescription[0] = '\0';
                quotationAmount = 0.0;
                if (!saveDatabase())
                    quotationMessage = "Database save failed: " + gDatabaseError;
                else
                    quotationMessage = "Quotation updated successfully.";
            }
        }

        ImGui::SameLine();

        if (ImGui::Button("Cancel Edit", ImVec2(130, 42)))
        {
            editingQuotationId = -1;
            quotationDescription[0] = '\0';
            quotationAmount = 0.0;
            quotationMessage.clear();
        }
    }

    if (!quotationMessage.empty())
    {
        ImGui::TextWrapped("%s", quotationMessage.c_str());
    }

    ImGui::Dummy(ImVec2(0, 16));
    ImGui::Separator();
    ImGui::Text("All Quotations");
    ImGui::Dummy(ImVec2(0, 8));

    int deleteRequested = -1;

    if (ImGui::BeginTable(
        "QuotationTable",
        8,
        ImGuiTableFlags_Borders |
        ImGuiTableFlags_RowBg |
        ImGuiTableFlags_Resizable |
        ImGuiTableFlags_ScrollY,
        ImVec2(0, 300)))
    {
        ImGui::TableSetupColumn("ID");
        ImGui::TableSetupColumn("Customer");
        ImGui::TableSetupColumn("Description");
        ImGui::TableSetupColumn("Amount");
        ImGui::TableSetupColumn("Date");
        ImGui::TableSetupColumn("Status");
        ImGui::TableSetupColumn("Edit");
        ImGui::TableSetupColumn("Delete");

        ImGui::TableHeadersRow();

        for (int i = 0; i < static_cast<int>(quotations.size()); i++)
        {
            const auto& quotation = quotations[i];

            ImGui::PushID(quotation.id);
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%d", quotation.id);

            ImGui::TableSetColumnIndex(1);
            ImGui::TextWrapped("%s", quotation.customerName.c_str());

            ImGui::TableSetColumnIndex(2);
            ImGui::TextWrapped("%s", quotation.description.c_str());

            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%.2f", quotation.amount);

            ImGui::TableSetColumnIndex(4);
            ImGui::TextUnformatted(quotation.date.c_str());

            ImGui::TableSetColumnIndex(5);
            ImGui::TextUnformatted(quotation.status.c_str());

            ImGui::TableSetColumnIndex(6);
            if (ImGui::SmallButton("Edit"))
            {
                editingQuotationId = quotation.id;
                selectedQuotationCustomer = 0;

                for (int c = 0; c < static_cast<int>(customers.size()); c++)
                {
                    if (customers[c].name == quotation.customerName)
                    {
                        selectedQuotationCustomer = c;
                        break;
                    }
                }

                std::snprintf(
                    quotationDescription,
                    sizeof(quotationDescription),
                    "%s",
                    quotation.description.c_str()
                );

                quotationAmount = quotation.amount;
                quotationMessage.clear();
            }

            ImGui::TableSetColumnIndex(7);
            if (ImGui::SmallButton("Delete"))
            {
                deleteRequested = quotation.id;
            }

            ImGui::PopID();
        }

        ImGui::EndTable();
    }

    if (quotations.empty())
        ImGui::TextDisabled("No quotations created yet.");

    if (deleteRequested != -1)
    {
        pendingDeleteQuotationId = deleteRequested;
        showDeletePopup = true;
        ImGui::OpenPopup("Confirm Quotation Delete");
    }

    if (ImGui::BeginPopupModal(
        "Confirm Quotation Delete",
        nullptr,
        ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Are you sure you want to delete this quotation?");
        ImGui::TextDisabled("This action cannot be undone.");
        ImGui::Dummy(ImVec2(0, 10));

        if (ImGui::Button("Yes, Delete", ImVec2(150, 45)))
        {
            for (auto it = quotations.begin(); it != quotations.end(); ++it)
            {
                if (it->id == pendingDeleteQuotationId)
                {
                    quotations.erase(it);
                    if (!saveDatabase())
                        quotationMessage = "Database save failed: " + gDatabaseError;
                    else
                        quotationMessage = "Quotation deleted successfully.";
                    break;
                }
            }

            if (editingQuotationId == pendingDeleteQuotationId)
            {
                editingQuotationId = -1;
                quotationDescription[0] = '\0';
                quotationAmount = 0.0;
            }

            pendingDeleteQuotationId = -1;
            showDeletePopup = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();

        if (ImGui::Button("Cancel", ImVec2(150, 45)))
        {
            pendingDeleteQuotationId = -1;
            showDeletePopup = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}


std::string escapeHTML(const std::string& text)
{
    std::string result;

    for (char c : text)
    {
        switch (c)
        {
            case '&': result += "&amp;"; break;
            case '<': result += "&lt;"; break;
            case '>': result += "&gt;"; break;
            case '"': result += "&quot;"; break;
            case '\'': result += "&#39;"; break;
            default: result += c; break;
        }
    }

    return result;
}

bool exportInvoiceToHTML(const Invoice& invoice)
{
    std::string filename =
        "invoice_" + std::to_string(invoice.id) + ".html";

    std::ofstream file(filename);

    if (!file.is_open())
        return false;

    double paid = invoice.paidAmount;

    if (paid < 0.0)
        paid = 0.0;

    if (paid > invoice.amount)
        paid = invoice.amount;

    double balance = invoice.amount - paid;

    std::string status;

    if (balance <= 0.001)
        status = "Paid";
    else if (paid > 0.001)
        status = "Partially Paid";
    else
        status = "Unpaid";

    file << R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Invoice #)HTML"
         << invoice.id << R"HTML(</title>
<style>
body {
    font-family: Arial, sans-serif;
    background: #f2f2f2;
    margin: 0;
    padding: 30px;
    color: #222;
}
.invoice {
    max-width: 850px;
    margin: auto;
    padding: 40px;
    background: white;
    border-top: 8px solid #eb5d18;
    box-shadow: 0 2px 12px rgba(0,0,0,0.12);
}
.header {
    display: flex;
    justify-content: space-between;
    gap: 20px;
    flex-wrap: wrap;
}
.company {
    color: #eb5d18;
    font-size: 26px;
    font-weight: bold;
}
.subtitle {
    color: #555;
    margin-top: 8px;
}
.invoice-title {
    font-size: 30px;
    font-weight: bold;
    text-align: right;
}
hr {
    border: 0;
    border-top: 1px solid #ddd;
    margin: 25px 0;
}
.details {
    display: flex;
    justify-content: space-between;
    gap: 20px;
    flex-wrap: wrap;
}
.label {
    color: #666;
    font-size: 13px;
}
.value {
    margin-top: 6px;
    font-weight: bold;
}
table {
    width: 100%;
    border-collapse: collapse;
    margin-top: 30px;
}
th {
    background: #242424;
    color: white;
    text-align: left;
    padding: 13px;
}
td {
    border-bottom: 1px solid #ddd;
    padding: 13px;
}
.number {
    text-align: right;
}
.totals {
    width: 320px;
    max-width: 100%;
    margin: 25px 0 0 auto;
}
.total-row {
    display: flex;
    justify-content: space-between;
    padding: 9px 0;
}
.balance {
    color: #eb5d18;
    font-size: 20px;
    font-weight: bold;
    border-top: 2px solid #eb5d18;
}
.status {
    display: inline-block;
    padding: 6px 12px;
    border-radius: 4px;
    background: #fff0e8;
    color: #a83b05;
    font-weight: bold;
}
.footer {
    margin-top: 45px;
    text-align: center;
    color: #666;
    font-size: 13px;
}
.actions {
    text-align: center;
    margin: 20px auto;
}
button {
    background: #eb5d18;
    color: white;
    border: 0;
    padding: 12px 22px;
    border-radius: 5px;
    cursor: pointer;
}
@media print {
    body { background: white; padding: 0; }
    .invoice { box-shadow: none; border-top: 6px solid #eb5d18; }
    .actions { display: none; }
}
</style>
</head>
<body>
<div class="actions">
<button onclick="window.print()">Print / Save as PDF</button>
</div>
<div class="invoice">
<div class="header">
<div>
<div class="company">)HTML"
         << escapeHTML(businessName) << R"HTML(</div>
<div class="subtitle">Professional Painting Services</div>
)HTML";

    if (!businessPhone.empty())
        file << "<div>Phone: " << escapeHTML(businessPhone)
             << "</div>\n";

    if (!businessEmail.empty())
        file << "<div>Email: " << escapeHTML(businessEmail)
             << "</div>\n";

    if (!businessAddress.empty())
        file << "<div>Address: " << escapeHTML(businessAddress)
             << "</div>\n";

    file << R"HTML(
</div>
<div>
<div class="invoice-title">INVOICE</div>
<div class="value">Invoice #)HTML"
         << invoice.id << R"HTML(</div>
<div class="value">Date: )HTML"
         << escapeHTML(invoice.date) << R"HTML(</div>
</div>
</div>
<hr>
<div class="details">
<div>
<div class="label">BILL TO</div>
<div class="value">)HTML"
         << escapeHTML(invoice.customerName) << R"HTML(</div>
</div>
<div>
<div class="label">QUOTATION REFERENCE</div>
<div class="value">#)HTML"
         << invoice.quotationId << R"HTML(</div>
</div>
</div>
<table>
<thead>
<tr>
<th>Description</th>
<th class="number">Amount</th>
</tr>
</thead>
<tbody>
<tr>
<td>)HTML"
         << escapeHTML(invoice.description) << R"HTML(</td>
<td class="number">)HTML"
         << std::fixed << std::setprecision(2)
         << invoice.amount << R"HTML(</td>
</tr>
</tbody>
</table>
<div class="totals">
<div class="total-row">
<span>Invoice Total</span>
<strong>)HTML"
         << invoice.amount << R"HTML(</strong>
</div>
<div class="total-row">
<span>Amount Paid</span>
<strong>)HTML"
         << paid << R"HTML(</strong>
</div>
<div class="total-row balance">
<span>Balance Due</span>
<span>)HTML"
         << balance << R"HTML(</span>
</div>
<div class="total-row">
<span>Status</span>
<span class="status">)HTML"
         << status << R"HTML(</span>
</div>
</div>
<div class="footer">
Thank you for your business.
</div>
</div>
</body>
</html>
)HTML";

    file.close();

    return !file.fail();
}

std::string getCurrentDate()
{
    std::time_t now = std::time(nullptr);
    std::tm* localTime = std::localtime(&now);

    if (localTime == nullptr)
    {
        return "";
    }

    std::ostringstream dateStream;
    dateStream << std::put_time(localTime, "%Y-%m-%d");

    return dateStream.str();
}


void drawInvoices()
{
    static int viewingInvoiceId = -1;
    static int editingInvoiceId = -1;
    static int deletingInvoiceId = -1;
    static bool openDeletePopup = false;

    static char searchText[128] = "";
    static int statusFilter = 0;

    static int quotationId = 0;
    static char description[256] = "";
    static double amount = 0.0;

    static char editDescription[256] = "";
    static char editDate[32] = "";
    static double editAmount = 0.0;
    static int loadedEditId = -1;

    ImGui::Text("INVOICE MANAGEMENT");
    ImGui::Separator();
    ImGui::Spacing();

    // CREATE INVOICE

    if (ImGui::CollapsingHeader(
            "Create New Invoice",
            ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::InputInt("Quotation ID", &quotationId);

        ImGui::InputText(
            "Invoice Description",
            description,
            sizeof(description));

        ImGui::InputDouble(
            "Invoice Amount ($)",
            &amount,
            0.0,
            0.0,
            "%.2f");

        ImGui::Text(
            "Invoice Date: %s",
            getCurrentDate().c_str());

        if (ImGui::Button("Create Invoice"))
        {
            if (description[0] == '\0' ||
                amount <= 0.0 ||
                quotationId < 0)
            {
                invoiceMessage =
                    "Enter a valid quotation ID, description, and amount.";
            }
            else
            {
                Invoice invoice{};

                invoice.id = nextInvoiceId++;
                invoice.quotationId = quotationId;
                invoice.customerName = "Customer";
                invoice.description = description;
                invoice.amount = amount;
                invoice.paidAmount = 0.0;
                invoice.date = getCurrentDate();
                invoice.status = "Unpaid";

                invoices.push_back(invoice);

                if (!saveDatabase())
                {
                    invoiceMessage =
                        "Database save failed: " + gDatabaseError;
                }
                else
                {
                    invoiceMessage =
                        "Invoice created successfully!";
                }

                quotationId = 0;
                description[0] = '\0';
                amount = 0.0;
            }
        }

        ImGui::SameLine();

        if (ImGui::Button("Clear Fields"))
        {
            quotationId = 0;
            description[0] = '\0';
            amount = 0.0;

            invoiceMessage = "Invoice fields cleared.";
        }
    }

    ImGui::Spacing();
    ImGui::Separator();

    // SEARCH AND FILTER

    ImGui::Text("SEARCH AND FILTER INVOICES");

    ImGui::InputText(
        "Search Invoice",
        searchText,
        sizeof(searchText));

    const char* statusOptions[] =
    {
        "All Statuses",
        "Unpaid",
        "Partially Paid",
        "Paid"
    };

    ImGui::Combo(
        "Filter by Status",
        &statusFilter,
        statusOptions,
        IM_ARRAYSIZE(statusOptions));

    ImGui::Spacing();

    // INVOICE TABLE

    if (ImGui::BeginTable(
            "InvoiceTable",
            11,
            ImGuiTableFlags_Borders |
            ImGuiTableFlags_RowBg |
            ImGuiTableFlags_Resizable |
            ImGuiTableFlags_ScrollX |
            ImGuiTableFlags_ScrollY,
            ImVec2(0, 300)))
    {
        ImGui::TableSetupColumn("ID");
        ImGui::TableSetupColumn("Quotation");
        ImGui::TableSetupColumn("Customer");
        ImGui::TableSetupColumn("Description");
        ImGui::TableSetupColumn("Amount");
        ImGui::TableSetupColumn("Date");
        ImGui::TableSetupColumn("Status");
        ImGui::TableSetupColumn("Details");
        ImGui::TableSetupColumn("Edit");
        ImGui::TableSetupColumn("Delete");
        ImGui::TableSetupColumn("Export");

        ImGui::TableHeadersRow();

        for (const auto& invoice : invoices)
        {
            std::string searchable =
                std::to_string(invoice.id) + " " +
                std::to_string(invoice.quotationId) + " " +
                invoice.customerName + " " +
                invoice.description + " " +
                invoice.status;

            std::string searchLower = searchable;
            std::string inputLower = searchText;

            std::transform(
                searchLower.begin(),
                searchLower.end(),
                searchLower.begin(),
                [](unsigned char c)
                {
                    return static_cast<char>(std::tolower(c));
                });

            std::transform(
                inputLower.begin(),
                inputLower.end(),
                inputLower.begin(),
                [](unsigned char c)
                {
                    return static_cast<char>(std::tolower(c));
                });

            if (!inputLower.empty() &&
                searchLower.find(inputLower) == std::string::npos)
            {
                continue;
            }

            if (statusFilter == 1 &&
                invoice.status != "Unpaid")
            {
                continue;
            }

            if (statusFilter == 2 &&
                invoice.status != "Partially Paid")
            {
                continue;
            }

            if (statusFilter == 3 &&
                invoice.status != "Paid")
            {
                continue;
            }

            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::Text("INV-%d", invoice.id);

            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%d", invoice.quotationId);

            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(invoice.customerName.c_str());

            ImGui::TableSetColumnIndex(3);
            ImGui::TextUnformatted(invoice.description.c_str());

            ImGui::TableSetColumnIndex(4);
            ImGui::Text("$%.2f", invoice.amount);

            ImGui::TableSetColumnIndex(5);
            ImGui::TextUnformatted(invoice.date.c_str());

            ImGui::TableSetColumnIndex(6);
            ImGui::TextUnformatted(invoice.status.c_str());

            ImGui::PushID(invoice.id);

            // DETAILS BUTTON

            ImGui::TableSetColumnIndex(7);

            if (ImGui::SmallButton("Details"))
            {
                viewingInvoiceId = invoice.id;
                editingInvoiceId = -1;
            }

            // EDIT BUTTON

            ImGui::TableSetColumnIndex(8);

            if (ImGui::SmallButton("Edit"))
            {
                editingInvoiceId = invoice.id;
                viewingInvoiceId = -1;
                loadedEditId = -1;
            }

            // DELETE BUTTON

            ImGui::TableSetColumnIndex(9);

            if (ImGui::SmallButton("Delete"))
            {
                deletingInvoiceId = invoice.id;
                openDeletePopup = true;
            }

            // EXPORT BUTTON

            ImGui::TableSetColumnIndex(10);

            if (ImGui::SmallButton("Export"))
            {
                exportInvoiceToHTML(invoice);
                invoiceMessage = "Invoice exported to HTML.";
            }

            ImGui::PopID();
        }

        ImGui::EndTable();
    }

    // OPEN DELETE POPUP OUTSIDE TABLE ID SCOPE

    if (openDeletePopup)
    {
        ImGui::OpenPopup("Confirm Invoice Deletion");
        openDeletePopup = false;
    }

    // MESSAGE

    if (!invoiceMessage.empty())
    {
        ImGui::Spacing();
        ImGui::TextWrapped("%s", invoiceMessage.c_str());
    }

    // DELETE CONFIRMATION POPUP

if (ImGui::BeginPopupModal(
        "Confirm Invoice Deletion",
        nullptr,
        ImGuiWindowFlags_AlwaysAutoResize))
{
    ImGui::Text(
        "Delete invoice INV-%d?",
        deletingInvoiceId);

    ImGui::Spacing();

    ImGui::TextWrapped(
        "This action cannot be undone. Any payment records linked to this invoice will also be deleted.");

    ImGui::Spacing();

    if (ImGui::Button("Yes, Delete", ImVec2(130, 40)))
    {
        auto it = std::find_if(
            invoices.begin(),
            invoices.end(),
            [](const Invoice& invoice)
            {
                return invoice.id == deletingInvoiceId;
            });

        if (it != invoices.end())
        {
            if (deleteInvoiceFromDatabase(deletingInvoiceId))
            {
                if (viewingInvoiceId == deletingInvoiceId)
                    viewingInvoiceId = -1;

                if (editingInvoiceId == deletingInvoiceId)
                {
                    editingInvoiceId = -1;
                    loadedEditId = -1;
                }

                payments.erase(
                    std::remove_if(
                        payments.begin(),
                        payments.end(),
                        [](const Payment& payment)
                        {
                            return payment.invoiceId == deletingInvoiceId;
                        }),
                    payments.end());

                invoices.erase(it);

                invoiceMessage =
                    "Invoice and its related payment records deleted successfully.";
            }
            else
            {
                invoiceMessage =
                    "Invoice deletion failed: " + gDatabaseError;
            }
        }
        else
        {
            invoiceMessage = "Invoice not found.";
        }

        deletingInvoiceId = -1;
        ImGui::CloseCurrentPopup();
    }

    ImGui::SameLine();

    if (ImGui::Button("Cancel", ImVec2(130, 40)))
    {
        deletingInvoiceId = -1;
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}
}
void drawPayments()
{
    static char paymentSearch[128] = "";
    static int paymentFilter = 0;

    ImGui::Text("Payment Management");
    ImGui::Separator();
    ImGui::Spacing();

    double totalPaymentsReceived = 0.0;

    for (const auto& p : payments)
    {
        totalPaymentsReceived += p.amount;
    }

    ImGui::Text("Total Payments Received");
    ImGui::PushStyleColor(
        ImGuiCol_Text,
        ImVec4(0.92f, 0.36f, 0.09f, 1.0f)
    );
    ImGui::Text("%.2f", totalPaymentsReceived);
    ImGui::PopStyleColor();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("Record a Payment");

    if (invoices.empty())
    {
        ImGui::TextWrapped(
            "No invoices available. Create an invoice first."
        );
    }
    else
    {
        std::string preview = "Select an invoice";

        if (selectedPaymentInvoice >= 0 &&
            selectedPaymentInvoice < (int)invoices.size())
        {
            const Invoice& inv = invoices[selectedPaymentInvoice];

            preview = "Invoice #" + std::to_string(inv.id) +
                      " - " + inv.customerName;
        }

        if (ImGui::BeginCombo("Invoice", preview.c_str()))
        {
            for (int i = 0; i < (int)invoices.size(); i++)
            {
                const Invoice& inv = invoices[i];
                double balance = inv.amount - inv.paidAmount;

                std::string label =
                    "Invoice #" + std::to_string(inv.id) +
                    " - " + inv.customerName +
                    " (Balance: " + std::to_string(balance) + ")";

                bool isSelected = (selectedPaymentInvoice == i);

                if (ImGui::Selectable(label.c_str(), isSelected))
                    selectedPaymentInvoice = i;

                if (isSelected)
                    ImGui::SetItemDefaultFocus();
            }

            ImGui::EndCombo();
        }

        if (selectedPaymentInvoice >= 0 &&
            selectedPaymentInvoice < (int)invoices.size())
        {
            Invoice& inv = invoices[selectedPaymentInvoice];
            double balance = inv.amount - inv.paidAmount;

            ImGui::Spacing();
            ImGui::Text("Customer: %s", inv.customerName.c_str());
            ImGui::Text("Invoice Amount: %.2f", inv.amount);
            ImGui::Text("Already Paid: %.2f", inv.paidAmount);
            ImGui::Text("Remaining Balance: %.2f", balance);

            ImGui::InputDouble("Payment Amount", &paymentAmount);

            if (ImGui::Button("Record Payment", ImVec2(180, 35)))
            {
                if (paymentAmount <= 0.0)
                {
                    paymentMessage =
                        "Payment must be greater than zero.";
                }
                else if (balance <= 0.000001)
                {
                    paymentMessage =
                        "This invoice is already fully paid.";
                }
                else if (paymentAmount > balance + 0.000001)
                {
                    paymentMessage =
                        "Payment cannot exceed the remaining balance.";
                }
                else
                {
                    Payment p;
                    p.id = nextPaymentId++;
                    p.invoiceId = inv.id;
                    p.customerName = inv.customerName;
                    p.amount = paymentAmount;
                    p.date = "Today";

                    payments.push_back(p);

                    inv.paidAmount += paymentAmount;

                    if (inv.paidAmount >= inv.amount - 0.000001)
                    {
                        inv.paidAmount = inv.amount;
                        inv.status = "Paid";
                    }
                    else
                    {
                        inv.status = "Partially Paid";
                    }

                    if (!saveDatabase())
                        paymentMessage = "Database save failed: " + gDatabaseError;
                    else
                        paymentMessage = "Payment recorded successfully!";

                    paymentAmount = 0.0;
                }
            }
        }
    }

    if (!paymentMessage.empty())
    {
        ImGui::Spacing();
        ImGui::TextWrapped("%s", paymentMessage.c_str());
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("Payment History");
    ImGui::Spacing();

    ImGui::SetNextItemWidth(300.0f);
    ImGui::InputTextWithHint(
        "##PaymentSearch",
        "Search customer or invoice ID...",
        paymentSearch,
        sizeof(paymentSearch)
    );

    ImGui::SameLine();

    const char* filterOptions[] = {
        "All Payments",
        "Invoice ID",
        "Customer Name"
    };

    ImGui::SetNextItemWidth(160.0f);
    ImGui::Combo(
        "Search By",
        &paymentFilter,
        filterOptions,
        IM_ARRAYSIZE(filterOptions)
    );

    ImGui::SameLine();

    if (ImGui::Button("Clear Search"))
    {
        paymentSearch[0] = '\0';
        paymentFilter = 0;
    }

    ImGui::Spacing();

    if (payments.empty())
    {
        ImGui::Text("No payments recorded yet.");
    }
    else
    {
        std::string searchText = paymentSearch;

        for (char& ch : searchText)
        {
            ch = (char)std::tolower((unsigned char)ch);
        }

        int matchingPayments = 0;
        double matchingTotal = 0.0;

        for (const auto& p : payments)
        {
            std::string customer = p.customerName;
            std::string invoiceId = std::to_string(p.invoiceId);

            for (char& ch : customer)
            {
                ch = (char)std::tolower((unsigned char)ch);
            }

            bool matches = searchText.empty();

            if (!searchText.empty())
            {
                if (paymentFilter == 0)
                {
                    matches =
                        customer.find(searchText) != std::string::npos ||
                        invoiceId.find(searchText) != std::string::npos;
                }
                else if (paymentFilter == 1)
                {
                    matches =
                        invoiceId.find(searchText) != std::string::npos;
                }
                else if (paymentFilter == 2)
                {
                    matches =
                        customer.find(searchText) != std::string::npos;
                }
            }

            if (matches)
            {
                matchingPayments++;
                matchingTotal += p.amount;
            }
        }

        ImGui::Text(
            "Matching Records: %d",
            matchingPayments
        );

        ImGui::SameLine(250.0f);

        ImGui::Text(
            "Matching Total: %.2f",
            matchingTotal
        );

        ImGui::Spacing();

        if (matchingPayments == 0)
        {
            ImGui::Text("No payments match your search.");
        }
        else if (ImGui::BeginTable(
                     "PaymentHistoryTable",
                     5,
                     ImGuiTableFlags_Borders |
                     ImGuiTableFlags_RowBg |
                     ImGuiTableFlags_Resizable |
                     ImGuiTableFlags_ScrollY,
                     ImVec2(0.0f, 300.0f)))
        {
            ImGui::TableSetupColumn("Payment ID");
            ImGui::TableSetupColumn("Invoice ID");
            ImGui::TableSetupColumn("Customer");
            ImGui::TableSetupColumn("Amount");
            ImGui::TableSetupColumn("Date");

            ImGui::TableHeadersRow();

            for (const auto& p : payments)
            {
                std::string customer = p.customerName;
                std::string invoiceId = std::to_string(p.invoiceId);

                for (char& ch : customer)
                {
                    ch = (char)std::tolower((unsigned char)ch);
                }

                bool matches = searchText.empty();

                if (!searchText.empty())
                {
                    if (paymentFilter == 0)
                    {
                        matches =
                            customer.find(searchText) != std::string::npos ||
                            invoiceId.find(searchText) != std::string::npos;
                    }
                    else if (paymentFilter == 1)
                    {
                        matches =
                            invoiceId.find(searchText) != std::string::npos;
                    }
                    else if (paymentFilter == 2)
                    {
                        matches =
                            customer.find(searchText) != std::string::npos;
                    }
                }

                if (!matches)
                    continue;

                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%d", p.id);

                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%d", p.invoiceId);

                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(p.customerName.c_str());

                ImGui::TableSetColumnIndex(3);
                ImGui::Text("%.2f", p.amount);

                ImGui::TableSetColumnIndex(4);
                ImGui::TextUnformatted(p.date.c_str());
            }

            ImGui::EndTable();
        }
    }
}

void drawFinancialReports()
{
    ImGui::Text("Financial Reports");
    ImGui::Separator();
    ImGui::Spacing();

    double totalInvoiced = 0.0;
    double totalPaid = 0.0;
    double totalOutstanding = 0.0;

    for (const auto& invoice : invoices)
    {
        totalInvoiced += invoice.amount;
        totalPaid += invoice.paidAmount;

        double balance = invoice.amount - invoice.paidAmount;

        if (balance > 0.000001)
            totalOutstanding += balance;
    }

    ImGui::Text("Revenue Summary");
    ImGui::Spacing();

    ImGui::Text("Total Invoiced: %.2f", totalInvoiced);
    ImGui::Text("Payments Received: %.2f", totalPaid);
    ImGui::Text("Outstanding Balance: %.2f", totalOutstanding);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("Payment Analytics");
    ImGui::Spacing();

    double paymentTotal = 0.0;

    for (const auto& payment : payments)
        paymentTotal += payment.amount;

    double averagePayment = payments.empty()
        ? 0.0
        : paymentTotal / payments.size();

    ImGui::Text("Total Payments Recorded: %.2f", paymentTotal);
    ImGui::Text("Number of Payments: %d", (int)payments.size());
    ImGui::Text("Average Payment Amount: %.2f", averagePayment);

    ImGui::Spacing();

    if (payments.empty())
    {
        ImGui::Text("No payment records available for analytics.");
    }
    else if (ImGui::BeginTable(
                 "PaymentAnalyticsTable",
                 3,
                 ImGuiTableFlags_Borders |
                 ImGuiTableFlags_RowBg |
                 ImGuiTableFlags_Resizable |
                 ImGuiTableFlags_ScrollX))
    {
        ImGui::TableSetupColumn("Customer");
        ImGui::TableSetupColumn("Number of Payments");
        ImGui::TableSetupColumn("Total Payments");

        ImGui::TableHeadersRow();

        std::vector<std::string> paymentCustomers;

        for (const auto& payment : payments)
        {
            bool found = false;

            for (const auto& name : paymentCustomers)
            {
                if (name == payment.customerName)
                {
                    found = true;
                    break;
                }
            }

            if (!found)
                paymentCustomers.push_back(payment.customerName);
        }

        for (const auto& name : paymentCustomers)
        {
            int paymentCount = 0;
            double customerPaymentTotal = 0.0;

            for (const auto& payment : payments)
            {
                if (payment.customerName == name)
                {
                    paymentCount++;
                    customerPaymentTotal += payment.amount;
                }
            }

            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(name.c_str());

            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%d", paymentCount);

            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%.2f", customerPaymentTotal);
        }

        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("Customer Payment Summary");
    ImGui::Spacing();

    if (invoices.empty())
    {
        ImGui::Text("No invoice data available for reports.");
    }
    else if (ImGui::BeginTable(
                 "CustomerFinancialSummary",
                 4,
                 ImGuiTableFlags_Borders |
                 ImGuiTableFlags_RowBg |
                 ImGuiTableFlags_Resizable |
                 ImGuiTableFlags_ScrollX))
    {
        ImGui::TableSetupColumn("Customer");
        ImGui::TableSetupColumn("Total Invoiced");
        ImGui::TableSetupColumn("Total Paid");
        ImGui::TableSetupColumn("Outstanding");

        ImGui::TableHeadersRow();

        std::vector<std::string> customerNames;

        for (const auto& invoice : invoices)
        {
            bool alreadyExists = false;

            for (const auto& name : customerNames)
            {
                if (name == invoice.customerName)
                {
                    alreadyExists = true;
                    break;
                }
            }

            if (!alreadyExists)
                customerNames.push_back(invoice.customerName);
        }

        for (const auto& name : customerNames)
        {
            double customerInvoiced = 0.0;
            double customerPaid = 0.0;
            double customerOutstanding = 0.0;

            for (const auto& invoice : invoices)
            {
                if (invoice.customerName == name)
                {
                    customerInvoiced += invoice.amount;
                    customerPaid += invoice.paidAmount;

                    double balance =
                        invoice.amount - invoice.paidAmount;

                    if (balance > 0.000001)
                        customerOutstanding += balance;
                }
            }

            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(name.c_str());

            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%.2f", customerInvoiced);

            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%.2f", customerPaid);

            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%.2f", customerOutstanding);
        }

        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("Invoice Aging Report");
    ImGui::TextWrapped(
        "Aging is calculated from invoice dates stored as YYYY-MM-DD. "
        "Invoices with dates such as Today are shown as undated."
    );
    ImGui::Spacing();

    time_t now = time(nullptr);
    tm* localTime = localtime(&now);

    int currentYear = 0;
    int currentMonth = 0;
    int currentDay = 0;

    if (localTime != nullptr)
    {
        currentYear = localTime->tm_year + 1900;
        currentMonth = localTime->tm_mon + 1;
        currentDay = localTime->tm_mday;
    }

    auto daysFromCivil = [](int year, unsigned month, unsigned day) -> long long
    {
        year -= month <= 2;
        const int era = (year >= 0 ? year : year - 399) / 400;
        const unsigned yearOfEra =
            (unsigned)(year - era * 400);
        const unsigned dayOfYear =
            (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5
            + day - 1;
        const unsigned dayOfEra =
            yearOfEra * 365 + yearOfEra / 4
            - yearOfEra / 100 + dayOfYear;

        return (long long)era * 146097 + dayOfEra;
    };

    double agingAmounts[4] = {0.0, 0.0, 0.0, 0.0};
    int agingCounts[4] = {0, 0, 0, 0};
    double undatedAmount = 0.0;
    int undatedCount = 0;

    for (const auto& invoice : invoices)
    {
        double balance = invoice.amount - invoice.paidAmount;

        if (balance <= 0.000001)
            continue;

        const std::string& date = invoice.date;

        if (date.size() != 10 ||
            date[4] != '-' ||
            date[7] != '-')
        {
            undatedAmount += balance;
            undatedCount++;
            continue;
        }

        int year = 0;
        int month = 0;
        int day = 0;

        try
        {
            year = std::stoi(date.substr(0, 4));
            month = std::stoi(date.substr(5, 2));
            day = std::stoi(date.substr(8, 2));
        }
        catch (...)
        {
            undatedAmount += balance;
            undatedCount++;
            continue;
        }

        if (month < 1 || month > 12 || day < 1 || day > 31)
        {
            undatedAmount += balance;
            undatedCount++;
            continue;
        }

        long long invoiceDay =
            daysFromCivil(year, (unsigned)month, (unsigned)day);

        long long today =
            daysFromCivil(currentYear, (unsigned)currentMonth,
                          (unsigned)currentDay);

        long long age = today - invoiceDay;

        if (age < 0)
        {
            undatedAmount += balance;
            undatedCount++;
        }
        else if (age <= 30)
        {
            agingAmounts[0] += balance;
            agingCounts[0]++;
        }
        else if (age <= 60)
        {
            agingAmounts[1] += balance;
            agingCounts[1]++;
        }
        else if (age <= 90)
        {
            agingAmounts[2] += balance;
            agingCounts[2]++;
        }
        else
        {
            agingAmounts[3] += balance;
            agingCounts[3]++;
        }
    }

    if (ImGui::BeginTable(
            "InvoiceAgingTable",
            3,
            ImGuiTableFlags_Borders |
            ImGuiTableFlags_RowBg |
            ImGuiTableFlags_Resizable))
    {
        ImGui::TableSetupColumn("Invoice Age");
        ImGui::TableSetupColumn("Outstanding Invoices");
        ImGui::TableSetupColumn("Outstanding Amount");

        ImGui::TableHeadersRow();

        const char* agingLabels[] = {
            "0-30 Days",
            "31-60 Days",
            "61-90 Days",
            "90+ Days"
        };

        for (int i = 0; i < 4; i++)
        {
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(agingLabels[i]);

            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%d", agingCounts[i]);

            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%.2f", agingAmounts[i]);
        }

        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        ImGui::Text("Undated");

        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%d", undatedCount);

        ImGui::TableSetColumnIndex(2);
        ImGui::Text("%.2f", undatedAmount);

        ImGui::EndTable();
    }
}


bool saveBusinessSettings()
{
    std::ofstream file("business_settings.txt");

    if (!file.is_open())
        return false;

    file << businessName << '\n';
    file << businessPhone << '\n';
    file << businessEmail << '\n';
    file << businessAddress << '\n';

    file.close();
    return !file.fail();
}

bool loadBusinessSettings()
{
    std::ifstream file("business_settings.txt");

    if (!file.is_open())
        return false;

    std::string name;
    std::string phone;
    std::string email;
    std::string address;

    if (!std::getline(file, name) ||
        !std::getline(file, phone) ||
        !std::getline(file, email) ||
        !std::getline(file, address))
    {
        return false;
    }

    if (name.empty())
        return false;

    businessName = name;
    businessPhone = phone;
    businessEmail = email;
    businessAddress = address;

    return true;
}


void drawSettings()
{
    static bool settingsLoaded = false;

    if (!settingsLoaded)
    {
        snprintf(
            businessNameInput,
            sizeof(businessNameInput),
            "%s",
            businessName.c_str()
        );

        snprintf(
            businessPhoneInput,
            sizeof(businessPhoneInput),
            "%s",
            businessPhone.c_str()
        );

        snprintf(
            businessEmailInput,
            sizeof(businessEmailInput),
            "%s",
            businessEmail.c_str()
        );

        snprintf(
            businessAddressInput,
            sizeof(businessAddressInput),
            "%s",
            businessAddress.c_str()
        );

        settingsLoaded = true;
    }

    ImGui::Text("Business Settings");
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("Business Profile");
    ImGui::TextWrapped(
        "Your business details will be saved locally on this computer."
    );

    ImGui::Spacing();

    ImGui::SetNextItemWidth(400.0f);
    ImGui::InputText(
        "Business Name",
        businessNameInput,
        sizeof(businessNameInput)
    );

    ImGui::SetNextItemWidth(400.0f);
    ImGui::InputText(
        "Phone Number",
        businessPhoneInput,
        sizeof(businessPhoneInput)
    );

    ImGui::SetNextItemWidth(400.0f);
    ImGui::InputText(
        "Email Address",
        businessEmailInput,
        sizeof(businessEmailInput)
    );

    ImGui::SetNextItemWidth(400.0f);
    ImGui::InputText(
        "Business Address",
        businessAddressInput,
        sizeof(businessAddressInput)
    );

    ImGui::Spacing();

    if (ImGui::Button("Save Business Profile", ImVec2(200, 40)))
    {
        if (std::string(businessNameInput).empty())
        {
            settingsMessage = "Business name cannot be empty.";
        }
        else
        {
            businessName = businessNameInput;
            businessPhone = businessPhoneInput;
            businessEmail = businessEmailInput;
            businessAddress = businessAddressInput;

            if (saveDatabase())
            {
                saveBusinessSettings();
                settingsMessage =
                    "Business profile saved successfully.";
            }
            else
            {
                settingsMessage =
                    "Database save failed: " + gDatabaseError;
            }
        }
    }

    if (!settingsMessage.empty())
    {
        ImGui::Spacing();
        ImGui::TextWrapped("%s", settingsMessage.c_str());
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("Invoice Branding Preview");
    ImGui::Spacing();

    ImGui::BeginChild(
        "InvoiceBrandingPreview",
        ImVec2(0, 180),
        true
    );

    ImGui::TextColored(
        ImVec4(0.92f, 0.36f, 0.09f, 1.0f),
        "%s",
        businessName.c_str()
    );

    ImGui::Separator();

    if (!businessPhone.empty())
        ImGui::Text("Phone: %s", businessPhone.c_str());

    if (!businessEmail.empty())
        ImGui::Text("Email: %s", businessEmail.c_str());

    if (!businessAddress.empty())
        ImGui::Text("Address: %s", businessAddress.c_str());

    if (businessPhone.empty() &&
        businessEmail.empty() &&
        businessAddress.empty())
    {
        ImGui::TextWrapped(
            "Add your contact details above to preview them here."
        );
    }

    ImGui::Spacing();
    ImGui::Text("PROFESSIONAL INVOICE");

    ImGui::EndChild();
}


void drawSection()
{
    if (selectedSection == 0)
        drawDashboard();
    else if (selectedSection == 1)
        drawCustomers();
    else if (selectedSection == 2)
        drawQuotations();
    else if (selectedSection == 3)
        drawInvoices();
    else if (selectedSection == 4)
        drawPayments();
    else if (selectedSection == 5)
        drawFinancialReports();
    else if (selectedSection == 6)
        drawSettings();
    else
        ImGui::Text("This section is coming soon.");
}

void drawStatCard(const char* title, const char* value,
                  const char* description, int targetSection);

int main(int, char**)
{
    if (!initializeDatabase())
    {
        printf("Database initialization failed: %s\n", gDatabaseError.c_str());
        return 1;
    }

    if (!loadDatabase())
    {
        printf("Database loading failed: %s\n", gDatabaseError.c_str());
        sqlite3_close(gDatabase);
        gDatabase = nullptr;
        return 1;
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0)
    {
        printf("SDL initialization failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "BizPilot",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        1280,
        800,
        SDL_WINDOW_RESIZABLE
    );

    if (window == nullptr)
    {
        printf("Window creation failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    // Set BizPilot application icon
SDL_Surface* appIcon = SDL_LoadBMP("assets/BizPilot_icon.bmp");

if (appIcon != nullptr)
{
    SDL_SetWindowIcon(window, appIcon);
    SDL_FreeSurface(appIcon);
}
else
{
    printf("Could not load BizPilot icon: %s\n", SDL_GetError());
}

    SDL_Renderer* renderer = SDL_CreateRenderer(
        window,
        -1,
        SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED
    );

    if (renderer == nullptr)
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);

    if (renderer == nullptr)
    {
        printf("Renderer creation failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    applyModernTheme();

    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);

    bool running = true;

    while (running)
    {
        SDL_Event event;

        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL2_ProcessEvent(&event);

            if (event.type == SDL_QUIT)
                running = false;

            if (event.type == SDL_WINDOWEVENT &&
                event.window.event == SDL_WINDOWEVENT_CLOSE)
                running = false;
        }

        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        ImGuiViewport* viewport = ImGui::GetMainViewport();

        ImGui::SetNextWindowPos(viewport->Pos);
        ImGui::SetNextWindowSize(viewport->Size);

        ImGui::PushStyleVar(
            ImGuiStyleVar_WindowPadding,
            ImVec2(0, 0)
        );

        ImGui::PushStyleVar(
            ImGuiStyleVar_WindowRounding,
            0
        );

        ImGui::PushStyleVar(
            ImGuiStyleVar_WindowBorderSize,
            0
        );

        ImGui::Begin(
            "ApplicationRoot",
            nullptr,
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoBringToFrontOnFocus
        );

        float sidebarWidth = 245;

        drawSidebar(sidebarWidth);
        ImGui::SameLine(0, 24);

        ImGui::PushStyleVar(
            ImGuiStyleVar_WindowPadding,
            ImVec2(24, 24)
        );

        ImGui::BeginChild("MainContent", ImVec2(0, 0), false);

        ImGui::Dummy(ImVec2(0, 2));
        drawSection();

        ImGui::EndChild();
        ImGui::PopStyleVar();

        ImGui::End();
        ImGui::PopStyleVar(3);

        ImGui::Render();

        SDL_SetRenderDrawColor(renderer, 245, 245, 245, 255);
        SDL_RenderClear(renderer);

        ImGui_ImplSDLRenderer2_RenderDrawData(
            ImGui::GetDrawData(),
            renderer
        );

        SDL_RenderPresent(renderer);
    }

    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    if (gDatabase)
    {
        sqlite3_close(gDatabase);
        gDatabase = nullptr;
    }

    return 0;
}
