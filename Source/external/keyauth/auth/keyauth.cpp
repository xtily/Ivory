#include "keyauth.h"

#include <shlobj.h>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <string>
#include <conio.h>
#include <Windows.h>
#include <keyauth/utils.hpp>

static std::string get_key_path() {
    std::string dir = skCrypt("C:\\Inhe\\Auth\\").decrypt();
    std::filesystem::create_directories(dir);
    return dir + skCrypt("session.json").decrypt();
}

static std::string read_input(const std::string& prompt, bool hidden = false) {
    std::cout << prompt;
    std::string input;
    if (hidden) {
        int c;
        while ((c = _getch()) != '\r' && c != '\n') {
            if (c == '\b') {
                if (!input.empty()) {
                    input.pop_back();
                    std::cout << "\b \b";
                }
            } else if (c != 0 && c != 0xE0) {
                input += (char)c;
                std::cout << '*';
            }
        }
        std::cout << "\n";
    } else {
        std::cin >> input;
    }
    return input;
}

void c_keyauth::session_status() {
    KeyAuthApp.check(true);
    if (!KeyAuthApp.response.success)
        exit(0);

    if (KeyAuthApp.response.isPaid) {
        while (true) {
            Sleep(20000);
            KeyAuthApp.check();
            if (!KeyAuthApp.response.success)
                exit(0);
        }
    }
}

bool c_keyauth::authenticate() {
    std::string key_path = get_key_path();

    KeyAuthApp.init();
    if (!KeyAuthApp.response.success) {
        std::cout << KeyAuthApp.response.message << "\n";
        Sleep(2000);
        exit(1);
    }

    std::string username, password;

    if (!key_path.empty() && std::filesystem::exists(key_path)) {
        std::string saved_user = ReadFromJson(key_path, skCrypt("username").decrypt());
        std::string saved_pass = ReadFromJson(key_path, skCrypt("password").decrypt());

        if (!saved_user.empty() && !saved_pass.empty()) {
            KeyAuthApp.login(saved_user, saved_pass);
            if (KeyAuthApp.response.success)
                goto auth_success;
            std::filesystem::remove(key_path);
        }
    }

    while (true) {
        std::cout << "[1] Register\n[2] Login\nInput: ";
        std::string choice;
        std::cin >> choice;
        std::cout << "\n";

        if (choice == "2") {
            username = read_input("Username: ");
            password = read_input("Password: ", true);
            std::cout << "\n";

            KeyAuthApp.login(username, password);

            if (!KeyAuthApp.response.success) {
                if (KeyAuthApp.response.message == "2FA code required.") {
                    std::string code = read_input("2FA Code: ");
                    std::cout << "\n";
                    KeyAuthApp.login(username, password, code);
                }
            }

            if (!KeyAuthApp.response.success) {
                std::cout << KeyAuthApp.response.message << "\n\n";
                Sleep(1500);
                system("cls");
                continue;
            }

            WriteToJson(key_path, skCrypt("username").decrypt(), username, true, skCrypt("password").decrypt(), password);
            break;

        } else if (choice == "1") {
            username = read_input("Username: ");
            password = read_input("Password: ", true);
            std::string license = read_input("License Key: ");
            std::cout << "\n";

            KeyAuthApp.regstr(username, password, license);

            if (!KeyAuthApp.response.success) {
                std::cout << KeyAuthApp.response.message << "\n\n";
                Sleep(1500);
                system("cls");
                continue;
            }

            KeyAuthApp.login(username, password);
            if (!KeyAuthApp.response.success) {
                std::cout << KeyAuthApp.response.message << "\n\n";
                Sleep(1500);
                system("cls");
                continue;
            }

            WriteToJson(key_path, skCrypt("username").decrypt(), username, true, skCrypt("password").decrypt(), password);
            break;

        } else {
            std::cout << "Invalid choice.\n\n";
            Sleep(1000);
            system("cls");
        }
    }

auth_success:
    system("cls");
    Sleep(300);

    std::thread check([&]() { session_status(); });
    check.detach();

    std::thread run(checkAuthenticated, ownerid);
    run.detach();

    return true;
}