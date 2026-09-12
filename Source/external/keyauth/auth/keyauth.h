#pragma once
#include <string>
#include <thread>
#include <memory>

#include <keyauth/auth.hpp>
#include <keyauth/sk.h>
#include <keyauth/utils.hpp>
#include <core/protection/xorstr.h>

using namespace KeyAuth;

class c_keyauth final
{
private:
	std::string name    = skCrypt("Inherently").decrypt();
	std::string ownerid = skCrypt("FW8qqqjWoS").decrypt();
	std::string version = skCrypt("1.0").decrypt();
	std::string url     = skCrypt("https://keyauth.win/api/1.3/").decrypt();
	std::string path    = skCrypt("").decrypt();

	api KeyAuthApp{ name, ownerid, version, url, path };
public:
	c_keyauth() {
		KeyAuthApp.require_pinning = true;
		KeyAuthApp.block_proxy = true;
		KeyAuthApp.block_custom_ca = true;
		KeyAuthApp.block_private_dns = true;
	}

	bool authenticate();
	void session_status();

	api& get_app() { return KeyAuthApp; }
	std::string get_ownerid() const { return ownerid; }
};

inline std::unique_ptr<c_keyauth> keyauth = std::make_unique<c_keyauth>();