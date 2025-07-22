// Include winsock2.h first to prevent conflicts
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winioctl.h>  // For CTL_CODE, FILE_DEVICE_UNKNOWN, etc.

#include "driver.h"
#include "simple_websocket.h"
#include <thread>
#include <chrono>

// PLayer name stuff
#include <string>
#include <iostream>


static DWORD getProcessId(const wchar_t* processName) {
	DWORD processId = 0;

	HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL);
	if (hSnapshot == INVALID_HANDLE_VALUE) {
		std::cout << "Failed to create snapshot of processes.\n";
		return processId;
	}

	PROCESSENTRY32 pe = {};
	pe.dwSize = sizeof(decltype(pe));

	if (Process32First(hSnapshot, &pe) == TRUE) {
		// Check if the first handle is the one we are looking for
		if (_wcsicmp(processName, pe.szExeFile) == 0) {
			processId = pe.th32ProcessID;
		}
		else {
			while (Process32NextW(hSnapshot, &pe) == TRUE) {
				if (_wcsicmp(processName, pe.szExeFile) == 0) {
					processId = pe.th32ProcessID;
					break;
				}
			}
		}
	}
	 
	CloseHandle(hSnapshot);
	return 0;
}

static std::uintptr_t getModuleBase(const DWORD pid, const wchar_t* moduleName) {
	std::uintptr_t moduleBase = 0;

	// Snapshot of the process modules (dlls)
	HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
	if (hSnapshot == INVALID_HANDLE_VALUE) {
		return moduleBase;
	}

	MODULEENTRY32W me = {};
	me.dwSize = sizeof(decltype(me));

	if (Module32FirstW(hSnapshot, &me) == TRUE) {
		if (wcsstr(moduleName, me.szModule) != nullptr) {
			moduleBase = reinterpret_cast<std::uintptr_t>(me.modBaseAddr);
		}
		else {
			while (Module32NextW(hSnapshot, &me) == TRUE) {
				if (wcsstr(moduleName, me.szModule) != nullptr) {
					moduleBase = reinterpret_cast<std::uintptr_t>(me.modBaseAddr);
					break;
				}
			}
		}
	}

	CloseHandle(hSnapshot);

	return moduleBase;
}

static std::string readStringFromMemory(HANDLE driverHandle, std::uintptr_t addr, std::uint32_t size) {
	std::string stringToBeRead;
	const auto strAddr = driver::readMemory<std::uintptr_t>(driverHandle, addr);

	if (strAddr != 0) {
		char* nameBuffer = (char*)malloc(size * sizeof(char));
		if (nameBuffer == nullptr) {
			std::cout << "Failed to allocate memory for string buffer.\n";
			return "";
		}
		// Read the string buffer from the address
		for (int j = 0; j < 127; ++j) {
			char c = driver::readMemory<char>(driverHandle, strAddr + j);
			if (c == '═') break;
			nameBuffer[j] = c;
		}
		stringToBeRead = std::string(nameBuffer);
		free(nameBuffer);
	}
	return stringToBeRead;
}

int main() {
	auto hwnd = FindWindowA(NULL, "Counter-Strike 2");
	DWORD pid; GetWindowThreadProcessId(hwnd, &pid);

	if (pid == 0) {
		std::cout << "Failed to find cs2\n";
		std::cin.get();
		return 1;
	}

	const HANDLE driverHandle{ CreateFile(L"\\\\.\\HappyDriver", GENERIC_READ, 0, nullptr,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr) };

	if (driverHandle == INVALID_HANDLE_VALUE) {
		std::cout << "Failed to create out driver handle.\n";
		std::cin.get();
		return 1;
	}

	if (driver::attachToProcess(driverHandle, pid) == true) {
		std::cout << "Attachment successful.\n";
		if (const std::uintptr_t client{ getModuleBase(pid, L"client.dll") }; client != 0) {
			std::cout << "Client found.\n";

			// Initialize and start websocket servers (flash events first, different port range)
			SimpleWebSocketServer wsServerLocalPlayerEvents;
			if (!wsServerLocalPlayerEvents.start(8001)) {
				std::cout << "Failed to start flash events websocket server on port 8001.\n";
				CloseHandle(driverHandle);
				std::cin.get();
				return 1;
			}

			SimpleWebSocketServer wsServer;
			if (!wsServer.start(8000)) {
				std::cout << "Failed to start main websocket server on port 8000.\n";
				CloseHandle(driverHandle);
				std::cin.get();
				return 1;
			}

			// Track previous values to detect changes
			float previousFlashEndTime = 0.0f;
			std::uint32_t previousLocalPlayerHealth = 0;

			while (true) {
				if (GetAsyncKeyState(VK_END))
					break;

				const auto localPlayerPawn = driver::readMemory<std::uintptr_t>(
					driverHandle, client + cs2_dumper::offsets::client_dll::dwLocalPlayerPawn);

				if (localPlayerPawn == 0)
					continue;

				const auto flags = driver::readMemory<std::uint32_t>(driverHandle,
					localPlayerPawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_fFlags);

				const float playerHeight = 63.83;;

				const auto entityList = driver::readMemory<std::uintptr_t>(
					driverHandle, client + cs2_dumper::offsets::client_dll::dwEntityList);

				const auto entityListEntry = driver::readMemory<std::uintptr_t>(driverHandle,
					entityList + 0x10);

				std::string entitiesData = "";

				const auto globalVars = driver::readMemory<std::uintptr_t>(driverHandle,
					client + cs2_dumper::offsets::client_dll::dwGlobalVars);

				std::string currentMap = readStringFromMemory(driverHandle,
					globalVars + 0x180, 128);

				if (currentMap.empty()) {
					std::cout << "Failed to read current map name. Maybe you're not in a game?.\n";
					continue;
				}

				const auto playerHealth = driver::readMemory<std::uint32_t>(driverHandle,
					localPlayerPawn
					+ cs2_dumper::schemas::client_dll::C_BaseEntity::m_iHealth);

				// Local player events - detect new flashbangs
				float flashEndTime = driver::readMemory<float>(driverHandle, localPlayerPawn + cs2_dumper::schemas::client_dll::C_CSPlayerPawnBase::m_flFlashBangTime);
				
				// Only send message when a new flashbang occurs (flashEndTime changes)
				if (flashEndTime != previousFlashEndTime && flashEndTime > 0.0f) {
					float currentGameTime = driver::readMemory<float>(driverHandle, globalVars + 0x34);
					float flashDuration = (flashEndTime > currentGameTime) ? (flashEndTime - currentGameTime) : 0.0f;
					
					// Send initial flash duration for this flashbang
					const std::string playerEvents = "flash:" + std::to_string(flashDuration);
					wsServerLocalPlayerEvents.broadcastData(playerEvents);
					
					std::cout << playerEvents << std::endl;
					
					// Update tracking variable
					previousFlashEndTime = flashEndTime;
				}

				if (playerHealth != previousLocalPlayerHealth) {
					const std::string localPlayerData = "localPlayer:" + std::to_string(playerHealth);
					// Update tracking variable
					previousLocalPlayerHealth = playerHealth;
					wsServerLocalPlayerEvents.broadcastData(localPlayerData);
				}
				
				// Sleep for 50 ms to avoid cpu overload
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
			}
		}
	}
	
	CloseHandle(driverHandle);
	std::cin.get();
	return 0;
}