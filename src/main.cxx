#include <SDL2/SDL.h>
#include <GL/glew.h>
#include <algorithm>
#include <ctime>
#include <fstream>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <string>
#include <vector>
#include <chrono>
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"
#include "guiconf.h"
#include "unistd.h"
#include "portable-file-dialogs.h"
using json=nlohmann::json;

typedef std::filesystem::path path;

[[nodiscard]] constexpr std::vector<std::string> darc_filters() {
	return {"Doom Archives", "*.wad *.WAD *.pk3 *.PK3"};
}

[[nodiscard]] const path iwad_dialog() {
	pfd::open_file file_selector("Select IWAD", ".",
			darc_filters(), pfd::opt::none);
	const std::vector<std::string> result = file_selector.result();
	if (result.empty()) return path("");
	return path(result[0]);
}

[[nodiscard]] const path single_pwad_dialog() {
	pfd::open_file file_selector("Select PWAD", ".",
			darc_filters(), pfd::opt::none);
	const std::vector<std::string> result = file_selector.result();
	if (result.empty()) return path("");
	return path(result[0]);
}

[[nodiscard]] const std::vector<path> pwad_dialog() {
	std::vector<path> pwads{};
	path result = single_pwad_dialog();
	while (!result.empty()) {
		pwads.push_back(result);
		result = single_pwad_dialog();
	}
	return pwads;
}

void add_pwad(const path& rootdir) {
	std::vector<path> pwad_paths = pwad_dialog();
	for (path pwad_path : pwad_paths)
		std::filesystem::copy_file(pwad_path, rootdir / "pwads" / pwad_path.filename());
}

void add_iwad(const path& rootdir) {
	path iwad_path = iwad_dialog();
	std::filesystem::copy_file(iwad_path, rootdir / "iwads" / iwad_path.filename());
}

void launch_doom(
		const path& rootdir, const path& iwad_path,
		const std::vector<path>& pwad_paths,
		const bool close_on_launch) {
	if (iwad_path.empty()) return;
	if (fork() == 0) {
		const std::size_t argc = 5+pwad_paths.size();
		const char** argv = new const char*[argc];
		argv[0] = "/usr/bin/gzdoom";
		argv[1] = "-iwad",
		argv[2] = iwad_path.c_str();
		argv[3] = "-file",
		argv[pwad_paths.size()+4] = nullptr;
		for (std::size_t i = 0; i < pwad_paths.size(); i++)
			argv[i+4] = pwad_paths[i].c_str();

		auto now = std::chrono::system_clock::now();
		std::time_t now_time = std::chrono::system_clock::to_time_t(now);
		char timestr[64];
		std::strftime(timestr, sizeof(timestr), "%d-%m-%Y-%T", std::localtime(&now_time));
		path logname = rootdir / "logs" / path(timestr);
		int fd = open(logname.c_str(),
				O_CREAT | O_TRUNC | O_WRONLY,
				S_IRUSR | S_IWUSR);
		dup2(fd, STDOUT_FILENO);

		execvp("gzdoom", const_cast<char* const*>(argv));
	} else if (close_on_launch) {
		exit(EXIT_SUCCESS);
	}
}

const std::vector<path> list_configs(const path& rootdir) {
	std::filesystem::directory_iterator dir_iter(rootdir / "configs");
	std::vector<path> available_config_paths{};
	for (path config_path : dir_iter)
		available_config_paths.push_back(config_path);
	return available_config_paths;
}

const std::vector<path> list_iwads(const path& rootdir) {
	std::filesystem::directory_iterator dir_iter(rootdir / "iwads");
	std::vector<path> available_iwad_paths{};
	for (path iwad_path : dir_iter)
		available_iwad_paths.push_back(iwad_path);
	return available_iwad_paths;
}

const std::vector<std::pair<path, bool>> list_available_pwads(const path& rootdir) {
	std::filesystem::directory_iterator dir_iter(rootdir / "pwads");
	std::vector<std::pair<path, bool>> pwad_paths{};
	for (path pwad_path : dir_iter)
		pwad_paths.push_back(std::make_pair(pwad_path, false));
	return pwad_paths;
}

const char* path_string_getter(void* data, int index) {
	path* paths = (path*)data;
	path& path_selected = paths[index];
	std::ptrdiff_t offset = path_selected.string().length() - path_selected.filename().string().length();
	return path_selected.c_str() + offset;
}

void load_config(
		const path& config_path, path& iwad_path,
		std::vector<path>& pwad_paths) {
	if (!std::filesystem::exists(config_path)) return;

	std::ifstream i(config_path);
	json j;
	i >> j;
	if (j.contains("iwad_path") && j["iwad_path"].is_string()
		&& std::filesystem::exists(path(j["iwad_path"]))) {
		iwad_path = path(j["iwad_path"]);
	}
	pwad_paths.clear();
	if (j.contains("pwad_paths") && j["pwad_paths"].is_array()) {
		for (json pwad : j["pwad_paths"]) {
			if (!pwad.is_string()) continue;
			if (!std::filesystem::exists(path(pwad))) continue;
			pwad_paths.push_back(path(pwad));
		}
	}
	i.close();
}

void save_config(
		const path& rootdir, char* config_name,
		const path& iwad_path, const std::vector<path>& pwad_paths) {
	if (!std::filesystem::exists(rootdir)) return;
	if (!std::filesystem::exists(rootdir / "configs")) return;
	
	path config_path = rootdir / "configs" / config_name;
	if (std::filesystem::exists(config_path)) {
		pfd::message overwrite("WARNING!",
				"Really overwrite config " +
				config_path.filename().string() + "? This is not reversible!",
				pfd::choice::ok_cancel, pfd::icon::warning);
		if (overwrite.result() != pfd::button::ok) return;
	}

	std::ofstream o(rootdir / "configs" / config_name);
	if (!o.is_open()) {
		std::cout << "couldnt open " << rootdir/config_name
			<< " in ostream to save" << std::endl;
		return;
	}
	json j;
	j["iwad_path"] = iwad_path;
	j["pwad_paths"] = {};
	for (path pwad : pwad_paths) {
		j["pwad_paths"].push_back(pwad);
	}
	std::cout << j << std::endl;
	o << j << std::endl;
	o.flush();
	o.close();
}

void delete_config(const path& config_path) {
	if (!std::filesystem::exists(config_path)) return;
	pfd::message message("WARNING!",
			"Really delete " +
			config_path.filename().string() + "? This is not reversible!",
			pfd::choice::ok_cancel, pfd::icon::warning);
	if (message.result() != pfd::button::ok) return;
	std::filesystem::remove(config_path);
}

int main(int argc, char** argv) {
	SDL_Init(SDL_INIT_VIDEO);

	const char* glsl_version = "#version 130";
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);

	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

	SDL_WindowFlags window_flags = (SDL_WindowFlags)(
			SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
	SDL_Window* window = SDL_CreateWindow("GZDoom Instancer",
			SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
			DEFAULT_WIN_SIZE_X, DEFAULT_WIN_SIZE_Y,
			window_flags);
	SDL_GLContext gl_context = SDL_GL_CreateContext(window);
	SDL_GL_MakeCurrent(window, gl_context);
	if (SDL_GL_SetSwapInterval(-1) == -1) SDL_GL_SetSwapInterval(1);

	glewExperimental = true;
	glewInit();

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

	ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
	ImGui_ImplOpenGL3_Init(glsl_version);

	ImFont* font = io.Fonts->AddFontFromFileTTF("ProggyClean.ttf", 26.0f);
	//ImFont* font = io.Fonts->AddFontFromFileTTF("ProggyTiny.ttf", 10.0f);
    //ImFont* font = io.Fonts->AddFontFromFileTTF("Jupiter.ttf", 18.0f);
	io.FontDefault = font;

	const path rootdir =
		path(getenv("HOME")) /
		path(".doominstancer");
	if (!std::filesystem::exists(rootdir))
		std::filesystem::create_directory(rootdir);
	if (!std::filesystem::exists(rootdir / "pwads"))
		std::filesystem::create_directory(rootdir / "pwads");
	if (!std::filesystem::exists(rootdir / "configs"))
		std::filesystem::create_directory(rootdir / "configs");
	if (!std::filesystem::exists(rootdir / "iwads"))
		std::filesystem::create_directory(rootdir / "iwads");
	if (!std::filesystem::exists(rootdir / "logs"))
		std::filesystem::create_directory(rootdir / "logs");

	bool running = true;

	path iwad_path("");
	std::vector<path> pwad_paths{};
	std::vector<std::pair<path, bool>> available_pwad_paths = list_available_pwads(rootdir);
	std::vector<path> available_config_paths = list_configs(rootdir);
	std::vector<path> available_iwad_paths = list_iwads(rootdir);

	char new_config_name[32];

	int last_config_index = -1;
	int current_config_index = 0;
	int current_iwad_index = 0;

	bool close_on_launch = false;

	while (running) {
		SDL_Event event;
		while (SDL_PollEvent(&event) > 0) {
			ImGui_ImplSDL2_ProcessEvent(&event);
			if (event.type == SDL_QUIT)
				running = false;
		}

		glClearColor(1,1,1,1);
		glClear(GL_COLOR_BUFFER_BIT);

		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplSDL2_NewFrame();
		ImGui::NewFrame();

		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
		ImGui::SetNextWindowPos(ImVec2{0,0});
		ImGui::SetNextWindowSize(io.DisplaySize);
		ImGui::Begin("MainWindow", NULL, 
				ImGuiWindowFlags_NoDecoration |
				ImGuiWindowFlags_NoResize);

		ImGui::Text("Root Directory: %s", rootdir.c_str());
		ImGui::ListBox("Config List", &current_config_index, path_string_getter,
				available_config_paths.data(), available_config_paths.size());
		if (ImGui::Button("Refresh Config List"))
			available_config_paths = list_configs(rootdir);
		if (ImGui::Button("Load Config")) {
			load_config(available_config_paths[current_config_index], iwad_path, pwad_paths);
			available_config_paths = list_configs(rootdir);
		}
		if (current_config_index != last_config_index) {
			path selected_config_path = available_config_paths[current_config_index];
			std::ptrdiff_t offset = selected_config_path.string().length() -
				selected_config_path.filename().string().length();
			memset(new_config_name, 0, 32ul); 
			strncpy(new_config_name, selected_config_path.c_str() + offset,
					std::min(selected_config_path.filename().string().length(), 31ul));
			last_config_index = current_config_index;
		}
		ImGui::InputText("Config Save Name", new_config_name, 31ul);
		if (ImGui::Button("Save Config")) {
			save_config(rootdir, new_config_name, iwad_path, pwad_paths);
			available_config_paths = list_configs(rootdir);
		}
		if (ImGui::Button("Delete Config")) {
			delete_config(available_config_paths[current_config_index]);
			available_config_paths = list_configs(rootdir);
		}

		ImGui::NewLine();

		ImGui::BeginTable("PWADs", 2, ImGuiTableFlags_SizingFixedFit |
				ImGuiTableFlags_BordersH | ImGuiTableFlags_BordersV |
				ImGuiTableFlags_NoHostExtendX);
		for (std::size_t i = 0; i < available_pwad_paths.size(); i++) {
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::Text("%s", available_pwad_paths[i].first.filename().c_str());
			ImGui::TableSetColumnIndex(1);
			ImGui::Checkbox("Active", &available_pwad_paths[i].second);
		}
		ImGui::EndTable();

		if (ImGui::Button("Refresh PWAD List"))
			available_pwad_paths = list_available_pwads(rootdir);
		if (ImGui::Button("Add PWAD")) {
			add_pwad(rootdir);
			available_pwad_paths = list_available_pwads(rootdir);
		}
		if (ImGui::Button("Activate Selected PWADs")) {
			for (std::pair<path, bool> available_pwad_path : available_pwad_paths) {
				if (!available_pwad_path.second) continue;
				if (std::find(pwad_paths.begin(), pwad_paths.end(),
						available_pwad_path.first) != pwad_paths.end()) continue;
				pwad_paths.push_back(available_pwad_path.first);
			}
		}
		if (ImGui::Button("Deactivate Selected PWADs")) {
			for (std::pair<path, bool> available_pwad_path : available_pwad_paths) {
				if (!available_pwad_path.second) continue;
				auto find = std::find(pwad_paths.begin(), pwad_paths.end(),
						available_pwad_path.first);
				if (find == pwad_paths.end()) continue;
				pwad_paths.erase(find);
			}
		}

		ImGui::NewLine();

		ImGui::ListBox("IWAD List", &current_iwad_index, path_string_getter,
				available_iwad_paths.data(), available_iwad_paths.size());
		if (ImGui::Button("Refresh IWAD List"))
			available_iwad_paths = list_iwads(rootdir);
		if (ImGui::Button("Add IWAD")) {
			add_iwad(rootdir);
			available_iwad_paths = list_iwads(rootdir);
		}
		if (ImGui::Button("Activate Selected IWAD")) {
			iwad_path = available_iwad_paths[current_iwad_index];
			available_iwad_paths = list_iwads(rootdir);
		}

		ImGui::NewLine();

		std::ptrdiff_t iwad_offset = iwad_path.string().length() -
			iwad_path.filename().string().length();
		ImGui::Text("Active IWAD: %s", iwad_path.empty() ? "<unset>" : iwad_path.c_str() + iwad_offset);
		ImGui::Text("Active PWADs:");
		for (path pwad_path : pwad_paths) {
			std::ptrdiff_t pwad_offset = pwad_path.string().length() -
				pwad_path.filename().string().length();
			ImGui::Text("\t%s", pwad_path.c_str() + pwad_offset);
		}

		ImGui::NewLine();

		ImGui::Checkbox("Close Instancer on Launch", &close_on_launch);
		if (ImGui::Button("Launch GZDoom"))
			launch_doom(rootdir, iwad_path, pwad_paths, close_on_launch);

		ImGui::End();
		ImGui::PopStyleVar(1);

		ImGui::Render();
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
		SDL_GL_SwapWindow(window);
	}

	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplSDL2_Shutdown();
	ImGui::DestroyContext();

	SDL_GL_DeleteContext(gl_context);
	SDL_DestroyWindow(window);
	SDL_Quit();

	return 0;
}
