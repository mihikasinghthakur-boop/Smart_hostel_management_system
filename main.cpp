#include "crow_all.h"
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>
#include <stack>
#include <queue>
#include <mutex>
#include <filesystem>

using namespace std;

mutex dataMutex; // Protects all shared state for multithreaded access

// ==========================================
// OOP CLASSES (Encapsulation)
// ==========================================

class Student {
public:
    string name;
    string id;
    int roomNumber;
    string type;
    string dateAllocated;

    Student(string n, string i, int r, string t, string d) 
        : name(n), id(i), roomNumber(r), type(t), dateAllocated(d) {}

    crow::json::wvalue toJson() const {
        crow::json::wvalue x;
        x["student"] = name;
        x["id"] = id;
        x["room"] = to_string(roomNumber);
        x["type"] = type;
        x["since"] = dateAllocated;
        return x;
    }
};

class Complaint {
public:
    string name;
    string room;
    string category;
    string priority;
    string desc;
    int queueNumber;

    Complaint(string n, string r, string c, string p, string d, int q) 
        : name(n), room(r), category(c), priority(p), desc(d), queueNumber(q) {}

    crow::json::wvalue toJson() const {
        crow::json::wvalue x;
        x["name"] = name;
        x["room"] = room;
        x["category"] = category;
        x["priority"] = priority;
        x["desc"] = desc;
        x["queue"] = queueNumber;
        return x;
    }
};

// ==========================================
// DSA STRUCTURES (Global State)
// ==========================================

vector<Student> allocatedStudents;   // Dynamic Array for active students
stack<int> singleRooms;              // LIFO Stack for available single rooms
queue<Complaint> complaintQueue;     // FIFO Queue for complaints
int totalComplaints = 0;

// Initialize the stack with some empty rooms (e.g., Floor 1 & 2)
void initializeRooms() {
    for (int i = 205; i >= 201; i--) singleRooms.push(i); // Rooms 201-205
    for (int i = 105; i >= 101; i--) singleRooms.push(i); // Rooms 101-105
}

// ==========================================
// MAIN SERVER LOGIC
// ==========================================

int main() {
    initializeRooms();
    crow::SimpleApp app;

    // Set template path relative to the executable location
    namespace fs = std::filesystem;
    fs::path exePath = fs::current_path();
    // Try common locations for the templates folder
    if (fs::exists(exePath / "templates")) {
        crow::mustache::set_global_base((exePath / "templates").string() + "/");
    } else if (fs::exists(exePath / ".." / "templates")) {
        crow::mustache::set_global_base(fs::canonical(exePath / ".." / "templates").string() + "/");
    }

    // Seed data (Using your name as the system admin/first student!)
    allocatedStudents.push_back(Student("Mihika Singh Thakur", "STU001", 101, "Single", "2026-03-30"));
    singleRooms.pop(); // Remove 101 from stack since it's taken
    complaintQueue.push(Complaint("Rahul Verma", "203", "WiFi/Network", "high", "Router is down.", ++totalComplaints));

    // ROUTE 1: Serve the HTML page
    CROW_ROUTE(app, "/")([]() {
        auto page = crow::mustache::load("hostel_management.html");
        return page.render();
    });

    // ROUTE 2: Get all allocations
    CROW_ROUTE(app, "/api/allocations").methods("GET"_method)
    ([]() {
        lock_guard<mutex> lock(dataMutex);
        vector<crow::json::wvalue> jsonList;
        for (const auto& student : allocatedStudents) {
            jsonList.push_back(student.toJson());
        }
        crow::json::wvalue finalJson;
        finalJson["allocations"] = std::move(jsonList);
        return finalJson;
    });

    // ROUTE 3: Allocate a new room (Uses Stack)
    CROW_ROUTE(app, "/api/allocate").methods("POST"_method)
    ([](const crow::request& req) {
        auto x = crow::json::load(req.body);
        if (!x) return crow::response(400, "Invalid Data");

        lock_guard<mutex> lock(dataMutex);
        if (singleRooms.empty()) return crow::response(400, "No rooms available!");

        // Pop from stack
        int room = singleRooms.top();
        singleRooms.pop();

        // Create object and push to vector
        string name = x["name"].s();
        string id = x["id"].s();
        string date = x["date"].s();
        
        allocatedStudents.push_back(Student(name, id, room, "Single", date));
        return crow::response(200, "Allocated Room " + to_string(room));
    });

    // ROUTE 4: Get all complaints
    CROW_ROUTE(app, "/api/complaints").methods("GET"_method)
    ([]() {
        lock_guard<mutex> lock(dataMutex);
        vector<crow::json::wvalue> jsonList;
        queue<Complaint> temp = complaintQueue; // Copy to read without popping
        while (!temp.empty()) {
            jsonList.push_back(temp.front().toJson());
            temp.pop();
        }
        crow::json::wvalue finalJson;
        finalJson["complaints"] = std::move(jsonList);
        return finalJson;
    });

    // ROUTE 5: Add a complaint (Uses Queue)
    CROW_ROUTE(app, "/api/complaints").methods("POST"_method)
    ([](const crow::request& req) {
        auto x = crow::json::load(req.body);
        if (!x) return crow::response(400);

        lock_guard<mutex> lock(dataMutex);
        Complaint c(x["name"].s(), x["room"].s(), x["category"].s(), x["priority"].s(), x["desc"].s(), ++totalComplaints);
        complaintQueue.push(c);
        return crow::response(200, "Complaint logged.");
    });

    // ROUTE 6: Resolve oldest complaint (Queue FIFO)
    CROW_ROUTE(app, "/api/complaints/resolve").methods("POST"_method)
    ([]() {
        lock_guard<mutex> lock(dataMutex);
        if (complaintQueue.empty()) return crow::response(400, "Queue empty.");
        complaintQueue.pop(); // Remove oldest
        return crow::response(200, "Resolved.");
    });

    // Use PORT env variable (for cloud deployment) or default to 18080
    int port = 18080;
    const char* envPort = getenv("PORT");
    if (envPort) port = stoi(envPort);

    cout << "\n[+] Server running at http://localhost:" << port << "\n";
    app.port(port).multithreaded().run();
}