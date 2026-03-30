#include "crow_all.h"
#include "sqlite3.h"
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>
#include <mutex>
#include <filesystem>

using namespace std;

mutex dbMutex;
sqlite3* db = nullptr;

// ==========================================
// DATABASE HELPERS
// ==========================================

void execSQL(const string& sql) {
    char* errMsg = nullptr;
    int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        cerr << "[DB ERROR] " << errMsg << endl;
        sqlite3_free(errMsg);
    }
}

void initDB() {
    int rc = sqlite3_open("nestiq.db", &db);
    if (rc) {
        cerr << "Cannot open database: " << sqlite3_errmsg(db) << endl;
        exit(1);
    }

    // Enable WAL mode for better concurrent performance
    execSQL("PRAGMA journal_mode=WAL;");

    // Create tables
    execSQL(R"(
        CREATE TABLE IF NOT EXISTS users (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            email TEXT UNIQUE NOT NULL,
            password TEXT NOT NULL,
            name TEXT NOT NULL,
            student_id TEXT DEFAULT '',
            role TEXT DEFAULT 'student'
        );
    )");

    execSQL(R"(
        CREATE TABLE IF NOT EXISTS allocations (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            student_name TEXT NOT NULL,
            student_id TEXT NOT NULL,
            room_number INTEGER NOT NULL,
            room_type TEXT DEFAULT 'Single',
            date_allocated TEXT NOT NULL
        );
    )");

    execSQL(R"(
        CREATE TABLE IF NOT EXISTS complaints (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL,
            room TEXT NOT NULL,
            category TEXT NOT NULL,
            priority TEXT NOT NULL,
            description TEXT NOT NULL,
            status TEXT DEFAULT 'open',
            created_at DATETIME DEFAULT CURRENT_TIMESTAMP
        );
    )");

    execSQL(R"(
        CREATE TABLE IF NOT EXISTS notices (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            title TEXT NOT NULL,
            tag TEXT DEFAULT 'general',
            content TEXT NOT NULL,
            author TEXT NOT NULL,
            date_posted TEXT NOT NULL
        );
    )");

    execSQL(R"(
        CREATE TABLE IF NOT EXISTS leaves (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL,
            student_id TEXT NOT NULL,
            from_date TEXT NOT NULL,
            to_date TEXT NOT NULL,
            reason TEXT NOT NULL,
            status TEXT DEFAULT 'pending'
        );
    )");

    // Seed admin account if not exists
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM users WHERE role='admin'", -1, &stmt, nullptr);
    sqlite3_step(stmt);
    int adminCount = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);

    if (adminCount == 0) {
        execSQL("INSERT INTO users (email, password, name, role) VALUES ('admin@nestiq.com', 'admin123', 'Admin', 'admin');");
        cout << "[+] Seeded admin account: admin@nestiq.com / admin123" << endl;
    }

    // Seed sample data if tables are empty
    sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM allocations", -1, &stmt, nullptr);
    sqlite3_step(stmt);
    int allocCount = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);

    if (allocCount == 0) {
        execSQL("INSERT INTO allocations (student_name, student_id, room_number, room_type, date_allocated) VALUES ('Mihika Singh Thakur', 'STU001', 101, 'Single', '2026-03-30');");
    }

    sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM complaints", -1, &stmt, nullptr);
    sqlite3_step(stmt);
    int compCount = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);

    if (compCount == 0) {
        execSQL("INSERT INTO complaints (name, room, category, priority, description) VALUES ('Rahul Verma', '203', 'WiFi/Network', 'high', 'Router is down.');");
    }

    sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM notices", -1, &stmt, nullptr);
    sqlite3_step(stmt);
    int noticeCount = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);

    if (noticeCount == 0) {
        execSQL("INSERT INTO notices (title, tag, content, author, date_posted) VALUES ('Water Supply Disruption', 'urgent', 'Water supply will be unavailable on 2nd April from 6AM-12PM due to pipeline maintenance. Please store water in advance.', 'Admin', '30 Mar 2026');");
    }

    cout << "[+] Database initialized successfully." << endl;
}

// ==========================================
// MAIN SERVER LOGIC
// ==========================================

int main() {
    initDB();
    crow::SimpleApp app;

    // Set template path
    namespace fs = std::filesystem;
    fs::path exePath = fs::current_path();
    if (fs::exists(exePath / "templates")) {
        crow::mustache::set_global_base((exePath / "templates").string() + "/");
    } else if (fs::exists(exePath / ".." / "templates")) {
        crow::mustache::set_global_base(fs::canonical(exePath / ".." / "templates").string() + "/");
    }

    // ── SERVE HTML ──
    CROW_ROUTE(app, "/")([]() {
        auto page = crow::mustache::load("hostel_management.html");
        return page.render();
    });

    // ══════════════════════════════════
    //  AUTH ROUTES
    // ══════════════════════════════════

    // REGISTER
    CROW_ROUTE(app, "/api/register").methods("POST"_method)
    ([](const crow::request& req) {
        auto x = crow::json::load(req.body);
        if (!x) return crow::response(400, R"({"error":"Invalid data"})");

        string email = x["email"].s();
        string password = x["password"].s();
        string name = x["name"].s();
        string studentId = x.has("student_id") ? string(x["student_id"].s()) : "";

        lock_guard<mutex> lock(dbMutex);

        // Check if email already exists
        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db, "SELECT id FROM users WHERE email=?", -1, &stmt, nullptr);
        sqlite3_bind_text(stmt, 1, email.c_str(), -1, SQLITE_TRANSIENT);
        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        if (rc == SQLITE_ROW) {
            return crow::response(409, R"({"error":"Email already registered"})");
        }

        // Insert new user
        sqlite3_prepare_v2(db, "INSERT INTO users (email, password, name, student_id, role) VALUES (?,?,?,?,'student')", -1, &stmt, nullptr);
        sqlite3_bind_text(stmt, 1, email.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, password.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, studentId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        crow::json::wvalue res;
        res["success"] = true;
        res["name"] = name;
        res["email"] = email;
        res["role"] = "student";
        res["student_id"] = studentId;
        return crow::response(200, res.dump());
    });

    // LOGIN
    CROW_ROUTE(app, "/api/login").methods("POST"_method)
    ([](const crow::request& req) {
        auto x = crow::json::load(req.body);
        if (!x) return crow::response(400, R"({"error":"Invalid data"})");

        string email = x["email"].s();
        string password = x["password"].s();

        lock_guard<mutex> lock(dbMutex);

        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db, "SELECT id, name, role, student_id FROM users WHERE email=? AND password=?", -1, &stmt, nullptr);
        sqlite3_bind_text(stmt, 1, email.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, password.c_str(), -1, SQLITE_TRANSIENT);

        int rc = sqlite3_step(stmt);
        if (rc != SQLITE_ROW) {
            sqlite3_finalize(stmt);
            return crow::response(401, R"({"error":"Invalid email or password"})");
        }

        crow::json::wvalue res;
        res["success"] = true;
        res["id"] = sqlite3_column_int(stmt, 0);
        res["name"] = string((const char*)sqlite3_column_text(stmt, 1));
        res["role"] = string((const char*)sqlite3_column_text(stmt, 2));
        const char* sid = (const char*)sqlite3_column_text(stmt, 3);
        res["student_id"] = sid ? string(sid) : "";
        res["email"] = email;
        sqlite3_finalize(stmt);

        return crow::response(200, res.dump());
    });

    // ══════════════════════════════════
    //  ALLOCATIONS
    // ══════════════════════════════════

    CROW_ROUTE(app, "/api/allocations").methods("GET"_method)
    ([]() {
        lock_guard<mutex> lock(dbMutex);
        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db, "SELECT student_name, student_id, room_number, room_type, date_allocated FROM allocations ORDER BY id DESC", -1, &stmt, nullptr);

        vector<crow::json::wvalue> list;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            crow::json::wvalue item;
            item["student"] = string((const char*)sqlite3_column_text(stmt, 0));
            item["id"] = string((const char*)sqlite3_column_text(stmt, 1));
            item["room"] = to_string(sqlite3_column_int(stmt, 2));
            item["type"] = string((const char*)sqlite3_column_text(stmt, 3));
            item["since"] = string((const char*)sqlite3_column_text(stmt, 4));
            list.push_back(move(item));
        }
        sqlite3_finalize(stmt);

        crow::json::wvalue res;
        res["allocations"] = move(list);
        return res;
    });

    CROW_ROUTE(app, "/api/allocate").methods("POST"_method)
    ([](const crow::request& req) {
        auto x = crow::json::load(req.body);
        if (!x) return crow::response(400, "Invalid Data");

        string name = x["name"].s();
        string id = x["id"].s();
        string date = x["date"].s();

        lock_guard<mutex> lock(dbMutex);

        // Find lowest available room (101-310) not already allocated
        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db, "SELECT room_number FROM allocations", -1, &stmt, nullptr);
        vector<int> usedRooms;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            usedRooms.push_back(sqlite3_column_int(stmt, 0));
        }
        sqlite3_finalize(stmt);

        int assignedRoom = -1;
        for (int floor = 1; floor <= 3; floor++) {
            for (int num = 1; num <= 10; num++) {
                int room = floor * 100 + num;
                bool used = false;
                for (int r : usedRooms) { if (r == room) { used = true; break; } }
                if (!used) { assignedRoom = room; break; }
            }
            if (assignedRoom > 0) break;
        }

        if (assignedRoom < 0) return crow::response(400, "No rooms available!");

        sqlite3_prepare_v2(db, "INSERT INTO allocations (student_name, student_id, room_number, room_type, date_allocated) VALUES (?,?,?,?,?)", -1, &stmt, nullptr);
        sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 3, assignedRoom);
        sqlite3_bind_text(stmt, 4, "Single", -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 5, date.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        return crow::response(200, "Allocated Room " + to_string(assignedRoom));
    });

    // ══════════════════════════════════
    //  COMPLAINTS
    // ══════════════════════════════════

    CROW_ROUTE(app, "/api/complaints").methods("GET"_method)
    ([]() {
        lock_guard<mutex> lock(dbMutex);
        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db, "SELECT id, name, room, category, priority, description FROM complaints WHERE status='open' ORDER BY id ASC", -1, &stmt, nullptr);

        vector<crow::json::wvalue> list;
        int q = 0;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            crow::json::wvalue item;
            item["dbId"] = sqlite3_column_int(stmt, 0);
            item["name"] = string((const char*)sqlite3_column_text(stmt, 1));
            item["room"] = string((const char*)sqlite3_column_text(stmt, 2));
            item["category"] = string((const char*)sqlite3_column_text(stmt, 3));
            item["priority"] = string((const char*)sqlite3_column_text(stmt, 4));
            item["desc"] = string((const char*)sqlite3_column_text(stmt, 5));
            item["queue"] = ++q;
            list.push_back(move(item));
        }
        sqlite3_finalize(stmt);

        crow::json::wvalue res;
        res["complaints"] = move(list);
        return res;
    });

    CROW_ROUTE(app, "/api/complaints").methods("POST"_method)
    ([](const crow::request& req) {
        auto x = crow::json::load(req.body);
        if (!x) return crow::response(400, "Invalid data");

        lock_guard<mutex> lock(dbMutex);
        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db, "INSERT INTO complaints (name, room, category, priority, description) VALUES (?,?,?,?,?)", -1, &stmt, nullptr);
        sqlite3_bind_text(stmt, 1, string(x["name"].s()).c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, string(x["room"].s()).c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, string(x["category"].s()).c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, string(x["priority"].s()).c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, string(x["desc"].s()).c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        return crow::response(200, "Complaint logged.");
    });

    CROW_ROUTE(app, "/api/complaints/resolve").methods("POST"_method)
    ([]() {
        lock_guard<mutex> lock(dbMutex);

        // Resolve the oldest open complaint (FIFO)
        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db, "UPDATE complaints SET status='resolved' WHERE id = (SELECT id FROM complaints WHERE status='open' ORDER BY id ASC LIMIT 1)", -1, &stmt, nullptr);
        sqlite3_step(stmt);
        int changes = sqlite3_changes(db);
        sqlite3_finalize(stmt);

        if (changes == 0) return crow::response(400, "Queue empty.");
        return crow::response(200, "Resolved.");
    });

    // ══════════════════════════════════
    //  NOTICES
    // ══════════════════════════════════

    CROW_ROUTE(app, "/api/notices").methods("GET"_method)
    ([]() {
        lock_guard<mutex> lock(dbMutex);
        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db, "SELECT title, tag, content, author, date_posted FROM notices ORDER BY id DESC", -1, &stmt, nullptr);

        vector<crow::json::wvalue> list;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            crow::json::wvalue item;
            item["title"] = string((const char*)sqlite3_column_text(stmt, 0));
            item["tag"] = string((const char*)sqlite3_column_text(stmt, 1));
            item["content"] = string((const char*)sqlite3_column_text(stmt, 2));
            item["author"] = string((const char*)sqlite3_column_text(stmt, 3));
            item["date"] = string((const char*)sqlite3_column_text(stmt, 4));
            list.push_back(move(item));
        }
        sqlite3_finalize(stmt);

        crow::json::wvalue res;
        res["notices"] = move(list);
        return res;
    });

    CROW_ROUTE(app, "/api/notices").methods("POST"_method)
    ([](const crow::request& req) {
        auto x = crow::json::load(req.body);
        if (!x) return crow::response(400, "Invalid data");

        lock_guard<mutex> lock(dbMutex);
        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db, "INSERT INTO notices (title, tag, content, author, date_posted) VALUES (?,?,?,?,?)", -1, &stmt, nullptr);
        sqlite3_bind_text(stmt, 1, string(x["title"].s()).c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, string(x["tag"].s()).c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, string(x["content"].s()).c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, string(x["author"].s()).c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, string(x["date"].s()).c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        return crow::response(200, "Notice posted.");
    });

    // ══════════════════════════════════
    //  LEAVE REQUESTS
    // ══════════════════════════════════

    CROW_ROUTE(app, "/api/leaves").methods("GET"_method)
    ([]() {
        lock_guard<mutex> lock(dbMutex);
        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db, "SELECT id, name, student_id, from_date, to_date, reason, status FROM leaves ORDER BY id DESC", -1, &stmt, nullptr);

        vector<crow::json::wvalue> list;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            crow::json::wvalue item;
            item["dbId"] = sqlite3_column_int(stmt, 0);
            item["name"] = string((const char*)sqlite3_column_text(stmt, 1));
            item["id"] = string((const char*)sqlite3_column_text(stmt, 2));
            item["from"] = string((const char*)sqlite3_column_text(stmt, 3));
            item["to"] = string((const char*)sqlite3_column_text(stmt, 4));
            item["reason"] = string((const char*)sqlite3_column_text(stmt, 5));
            item["status"] = string((const char*)sqlite3_column_text(stmt, 6));
            list.push_back(move(item));
        }
        sqlite3_finalize(stmt);

        crow::json::wvalue res;
        res["leaves"] = move(list);
        return res;
    });

    CROW_ROUTE(app, "/api/leaves").methods("POST"_method)
    ([](const crow::request& req) {
        auto x = crow::json::load(req.body);
        if (!x) return crow::response(400, "Invalid data");

        lock_guard<mutex> lock(dbMutex);
        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db, "INSERT INTO leaves (name, student_id, from_date, to_date, reason) VALUES (?,?,?,?,?)", -1, &stmt, nullptr);
        sqlite3_bind_text(stmt, 1, string(x["name"].s()).c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, string(x["id"].s()).c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, string(x["from"].s()).c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, string(x["to"].s()).c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, string(x["reason"].s()).c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        return crow::response(200, "Leave submitted.");
    });

    CROW_ROUTE(app, "/api/leaves/update").methods("POST"_method)
    ([](const crow::request& req) {
        auto x = crow::json::load(req.body);
        if (!x) return crow::response(400, "Invalid data");

        int leaveId = x["id"].i();
        string status = x["status"].s();

        lock_guard<mutex> lock(dbMutex);
        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db, "UPDATE leaves SET status=? WHERE id=?", -1, &stmt, nullptr);
        sqlite3_bind_text(stmt, 1, status.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, leaveId);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        return crow::response(200, "Leave " + status + ".");
    });

    // Use PORT env variable or default to 18080
    int port = 18080;
    const char* envPort = getenv("PORT");
    if (envPort) port = stoi(envPort);

    cout << "\n[+] Server running at http://localhost:" << port << "\n";
    app.port(port).multithreaded().run();

    sqlite3_close(db);
}