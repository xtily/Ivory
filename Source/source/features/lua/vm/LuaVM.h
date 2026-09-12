#pragma once
#include <string>
#include <cstdint>
#include <functional>

struct lua_State;

namespace LuaVM {
    void RenderStandaloneWindow();
    bool Initialize();

void Shutdown();


bool Execute(const std::string& source, const char* chunk_name = "script");

lua_State* State();
bool Ready();


void Tick(float dt);


void ScheduleWait(lua_State* L, float sec);





void PushSignal(lua_State* L, int kind, std::uint64_t owner = 0, const char* prop = nullptr);
void FireSignal(lua_State* L, int kind, std::uint64_t owner, const char* prop);

struct LogEntry {
    std::string message;
    bool is_error = false;
};

std::vector<LogEntry> GetLogs();
void ClearLogs();


void LogOutput(const char* msg, bool is_error = false);


void SetLogCallback(std::function<void(const char*, bool)> cb);

}