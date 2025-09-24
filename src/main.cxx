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
#include <curl/curl.h>
#include <zip.h>
using json=nlohmann::json;

typedef std::filesystem::path path;

class GZDoomInstancer {
	public:
	GZDoomInstancer() {
		rootdir =
			path(getenv("HOME")) /
			path(".doominstancer");
		if (!std::filesystem::exists(rootdir))
			std::filesystem::create_directory(rootdir);
		if (!std::filesystem::exists(rootdir / "pwads"))
			std::filesystem::create_directory(rootdir / "pwads");
		if (!std::filesystem::exists(rootdir / "instances"))
			std::filesystem::create_directory(rootdir / "instances");
		if (!std::filesystem::exists(rootdir / "iwads"))
			std::filesystem::create_directory(rootdir / "iwads");
		if (!std::filesystem::exists(rootdir / "logs"))
			std::filesystem::create_directory(rootdir / "logs");

		iwad_path = "";
		pwad_paths = {};
		available_pwad_paths = list_available_pwads();
		available_instance_paths = list_instances();
		available_iwad_paths = list_iwads();
		#ifdef WIN32
		gzdoom_path = "";
		#else
		gzdoom_path = "/usr/games/gzdoom";
		#endif
	}

	[[nodiscard]] constexpr std::vector<std::string> darc_filters() {
		return {"Doom Archives", "*.wad *.WAD *.pk3 *.PK3"};
	}

	[[nodiscard]] constexpr const char* signature() {
		return "GZDoom Instancer is FOSS developed by @timerunner16.";
	}

	[[nodiscard]] const path iwad_dialog() {
		pfd::open_file file_selector("Select IWAD", ".",
				darc_filters(), pfd::opt::none);
		const std::vector<std::string> result = file_selector.result();
		if (result.empty()) return path("");
		return path(result[0]);
	}

	[[nodiscard]] const std::vector<path> pwad_dialog() {
		pfd::open_file file_selector("Select PWADs", ".",
				darc_filters(), pfd::opt::multiselect);
		const std::vector<std::string> result = file_selector.result();
		std::vector<path> paths{};
		for (std::string i : result)
			paths.push_back(path(i));
		return paths;
	}

	[[nodiscard]] const path gzdoom_dialog() {
		pfd::open_file file_selector("Select GZDoom Executable", ".",
				{"All Files", "*"}, pfd::opt::none);
		const std::vector<std::string> result = file_selector.result();
		if (result.empty()) return path("");
		return path(result[0]);
	}

	void add_pwad() {
		std::vector<path> pwad_paths = pwad_dialog();
		for (path pwad_path : pwad_paths)
			std::filesystem::copy_file(pwad_path, rootdir / "pwads" / pwad_path.filename());
	}

	void add_iwad() {
		path iwad_path = iwad_dialog();
		std::filesystem::copy_file(iwad_path, rootdir / "iwads" / iwad_path.filename());
	}

	void download_idgames() {
		std::string file(current_idgames_file);
		CURL* curl = curl_easy_init();
		if (!curl) return;
		CURLcode res;
		std::string destination =
			std::string("https://www.quaddicted.com/files/idgames/") + file;
		path temp_output = rootdir / "downloads/temp.zip";
		curl_easy_setopt(curl, CURLOPT_URL, destination.c_str());
		FILE* fd = fopen(temp_output.c_str(), "wb");
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, fd);
		res = curl_easy_perform(curl);
		curl_easy_cleanup(curl);
		fclose(fd);

		pfd::notify notify("IDGames Download",
				"PWAD Download finished, beginning install.", pfd::icon::info);
		notify.ready();

		int err;
		zip_t* z = zip_open(temp_output.c_str(), ZIP_RDONLY, &err);
		zip_stat_t sb;
		
		std::string name = path(file).filename().replace_extension("").string();
		std::string outname;

		std::size_t i;
		for (i = 0; i < zip_get_num_entries(z, 0); i++) {
			if (zip_stat_index(z, i, 0, &sb) != 0) continue;
			outname = std::string(sb.name);
			if (outname.starts_with(name) && !outname.ends_with(".txt")) {
				break;
			}
		}
		char* contents = new char[sb.size];
		zip_file* f = zip_fopen(z, sb.name, 0);
		zip_fread(f, contents, sb.size);
		zip_fclose(f);
		zip_close(z);

		std::ofstream(rootdir / "pwads" / outname).write(contents, sb.size);
		std::filesystem::remove(temp_output);
	}

	void launch_doom() {
		const path instance_path = available_instance_paths[current_instance_index];
		if (!std::filesystem::exists(instance_path)) return;
		if (!std::filesystem::is_directory(instance_path)) return;
		if (!std::filesystem::exists(gzdoom_path)) return;
		if (std::filesystem::is_directory(gzdoom_path)) return;
		if (std::filesystem::perms::none == 
				(std::filesystem::status(gzdoom_path).permissions() &
				std::filesystem::perms::owner_exec)) return;
		if (fork() == 0) {
			std::vector<path> pwad_paths;
			path iwad_path;
			load_instance();
			path save_path = instance_path / "save";
			path config_path = instance_path / "config";

			const std::size_t argc = 9+pwad_paths.size();
			const char** argv = new const char*[argc];
			argv[0] = gzdoom_path.c_str();
			argv[1] = "-iwad",
			argv[2] = iwad_path.c_str();
			argv[3] = "-savedir";
			argv[4] = save_path.c_str();
			argv[5] = "-config";
			argv[6] = config_path.c_str();
			argv[7] = "-file",
			argv[pwad_paths.size()+8] = nullptr;
			for (std::size_t i = 0; i < pwad_paths.size(); i++)
				argv[i+8] = pwad_paths[i].c_str();

			auto now = std::chrono::system_clock::now();
			std::time_t now_time = std::chrono::system_clock::to_time_t(now);
			char timestr[64];
			std::strftime(timestr, sizeof(timestr), "%d-%m-%Y-%T", std::localtime(&now_time));
			path logname = rootdir / "logs" / path(timestr);
			int fd = open(logname.c_str(),
					O_CREAT | O_TRUNC | O_WRONLY,
					S_IRUSR | S_IWUSR);
			dup2(fd, STDOUT_FILENO);

			execvp(gzdoom_path.c_str(), const_cast<char* const*>(argv));
		} else if (close_on_launch) {
			exit(EXIT_SUCCESS);
		}
	}

	const std::vector<path> list_instances() {
		std::filesystem::directory_iterator dir_iter(rootdir / "instances");
		std::vector<path> available_instance_paths{};
		for (path instance_path : dir_iter)
			available_instance_paths.push_back(instance_path);
		std::sort(available_instance_paths.begin(), available_instance_paths.end());
		return available_instance_paths;
	}

	const std::vector<path> list_iwads() {
		std::filesystem::directory_iterator dir_iter(rootdir / "iwads");
		std::vector<path> available_iwad_paths{};
		for (path iwad_path : dir_iter)
			available_iwad_paths.push_back(iwad_path);
		std::sort(available_iwad_paths.begin(), available_iwad_paths.end());
		return available_iwad_paths;
	}

	const std::vector<std::pair<path, bool>> list_available_pwads() {
		std::filesystem::directory_iterator dir_iter(rootdir / "pwads");
		std::vector<std::pair<path, bool>> pwad_paths{};
		for (path pwad_path : dir_iter)
			pwad_paths.push_back(std::make_pair(pwad_path, false));
		std::sort(pwad_paths.begin(), pwad_paths.end(), [](auto a, auto b) -> bool {
			return a.first < b.first;
		});
		return pwad_paths;
	}

	static const char* path_string_getter(void* data, int index) {
		path* paths = (path*)data;
		path& path_selected = paths[index];
		std::ptrdiff_t offset = path_selected.string().length() -
			path_selected.filename().string().length();
		return path_selected.c_str() + offset;
	}

	void load_instance() {
		const path instance_path = available_instance_paths[current_instance_index];
		if (!std::filesystem::exists(instance_path)) return;

		std::ifstream i(instance_path / "config.json");
		json j;
		i >> j;
		iwad_path.clear();
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

	void save_instance() {
		if (!std::filesystem::exists(rootdir)) return;
		if (!std::filesystem::exists(rootdir / "instances")) return;
		
		path instance_path = rootdir / "instances" / new_instance_name;
		if (std::filesystem::exists(instance_path)) {
			pfd::message overwrite("WARNING!",
					"Really overwrite instance " +
					instance_path.filename().string() + "? This is not reversible!",
					pfd::choice::ok_cancel, pfd::icon::warning);
			if (overwrite.result() != pfd::button::ok) return;
		} else {
			std::filesystem::create_directory(rootdir / "instances" / new_instance_name);
			std::filesystem::create_directory(rootdir / "instances" / new_instance_name / "save");
		}

		std::ofstream o(instance_path / "config.json");
		if (!o.is_open()) {
			std::cout << "couldnt open " << rootdir/new_instance_name
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

	void delete_instance() {
		const path instance_path = available_instance_paths[current_instance_index];
		if (!std::filesystem::exists(instance_path)) return;
		pfd::message message("WARNING!",
				"Really delete " +
				instance_path.filename().string() + "? This is not reversible!",
				pfd::choice::ok_cancel, pfd::icon::warning);
		if (message.result() != pfd::button::ok) return;
		std::filesystem::remove_all(instance_path);
	}

	void duplicate_instance() {
		const path og_instance_path = available_instance_paths[current_instance_index];
		const path new_instance_path = rootdir / "instances" / new_instance_name;
		std::filesystem::copy(og_instance_path, new_instance_path,
				std::filesystem::copy_options::recursive);
	}

	void manager_launcher_view() {
		ImVec2 size = ImGui::GetContentRegionAvail();
		size.y -= ImGui::GetTextLineHeightWithSpacing();

		ImGui::BeginTable("##", 2, ImGuiTableFlags_BordersInnerV |
				ImGuiTableFlags_RowBg |
				ImGuiTableFlags_Hideable | ImGuiTableFlags_Reorderable,
				size);
		ImGui::TableSetupColumn("Instance Manager");
		ImGui::TableSetupColumn("Game Launcher");
		ImGui::TableHeadersRow();

		ImGui::TableNextColumn();

		ImGui::ListBox("Instances", &current_instance_index,
				this->path_string_getter,
				available_instance_paths.data(), available_instance_paths.size());
		if (ImGui::Button("Refresh"))
			available_instance_paths = list_instances();
		if (current_instance_index != last_instance_index) {
			path selected_instance_path = 
				available_instance_paths[current_instance_index];
			std::ptrdiff_t offset = selected_instance_path.string().length() -
				selected_instance_path.filename().string().length();
			memset(new_instance_name, 0, 32ul); 
			strncpy(new_instance_name, selected_instance_path.c_str() + offset,
					std::min(selected_instance_path.filename().string().length(), 31ul));
			last_instance_index = current_instance_index;
		}
		ImGui::InputText("New Name", new_instance_name, 31ul);
		if (ImGui::Button("Duplicate")) {
			path new_instance_path = rootdir / "instances" / new_instance_name;
			if (!std::filesystem::exists(new_instance_path)) {
				duplicate_instance();
				available_instance_paths = list_instances();
				int new_instance_index = std::distance(available_instance_paths.begin(),
						std::find(available_instance_paths.begin(),
						available_instance_paths.end(),
						new_instance_path));
				if (new_instance_index < available_instance_paths.size())
					current_instance_index = new_instance_index;
			} else {
				pfd::message message("ERROR!", "An instance already exists with this " \
						"name. Aborting creation of new instance.", pfd::choice::ok,
						pfd::icon::error);
				message.ready();
			}
		}
		if (ImGui::Button("New")) {
			path new_instance_path = rootdir / "instances" / new_instance_name;
			if (!std::filesystem::exists(new_instance_path)) {
				iwad_path = "";
				pwad_paths = {};
				save_instance();
				available_instance_paths = list_instances();
				int new_instance_index = std::distance(available_instance_paths.begin(),
						std::find(available_instance_paths.begin(),
						available_instance_paths.end(),
						new_instance_path));
				if (new_instance_index < available_instance_paths.size())
					current_instance_index = new_instance_index;
			} else {
				pfd::message message("ERROR!", "An instance already exists with this " \
						"name. Aborting creation of new instance.", pfd::choice::ok,
						pfd::icon::error);
				message.ready();
			}
		}
		if (ImGui::Button("Delete")) {
			delete_instance();
			available_instance_paths = list_instances();
		}
		if (ImGui::Button("Edit")) {
			load_instance();
			current_view = EDITOR_VIEW;
		}

		ImGui::TableNextColumn();

		ImGui::Checkbox("Close Instancer on Launch", &close_on_launch);
		std::string button_name = std::string("Launch ") +
			available_instance_paths[current_instance_index].filename().string();
		if (ImGui::Button("Set GZDoom Path"))
			gzdoom_path = gzdoom_dialog();
		ImGui::TextWrapped("GZDoom Path: %s",
				gzdoom_path.empty() ? "<unset>" : gzdoom_path.c_str());
		if (ImGui::Button(button_name.c_str()))
			launch_doom();

		ImGui::EndTable();

		ImGui::Text("%s", signature());
	}

	void editor_view() {
		ImVec2 size = ImGui::GetContentRegionAvail();
		size.y -= ImGui::GetTextLineHeightWithSpacing();

		ImGui::BeginTable("##", 1, ImGuiTableFlags_BordersInnerV |
				ImGuiTableFlags_RowBg |
				ImGuiTableFlags_Hideable | ImGuiTableFlags_Reorderable,
				size);
		std::string name = std::string("Editing instance " +
				available_instance_paths[current_instance_index].filename().string());
		ImGui::TableSetupColumn(name.c_str());
		ImGui::TableHeadersRow();

		ImGui::TableNextColumn();

		if (ImGui::BeginTable("PWADs", 2, ImGuiTableFlags_SizingFixedFit |
				ImGuiTableFlags_BordersH | ImGuiTableFlags_BordersV |
				ImGuiTableFlags_NoHostExtendX)) {
			for (std::size_t i = 0; i < available_pwad_paths.size(); i++) {
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("%s", available_pwad_paths[i].first.filename().c_str());
				ImGui::TableSetColumnIndex(1);
				std::string cbname = std::string("##") + std::to_string(i);
				ImGui::Checkbox(cbname.c_str(), &available_pwad_paths[i].second);
			}
			ImGui::EndTable();
		}

		if (ImGui::Button("Refresh PWAD List"))
			available_pwad_paths = list_available_pwads();
		if (ImGui::Button("Add PWAD")) {
			add_pwad();
			available_pwad_paths = list_available_pwads();
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

		ImGui::InputText("IDGames File", current_idgames_file, 64);
		if (ImGui::Button("Download PWAD"))
			download_idgames();

		ImGui::NewLine();

		ImGui::Text("Active PWADs:");
		for (path pwad_path : pwad_paths) {
			std::ptrdiff_t pwad_offset = pwad_path.string().length() -
				pwad_path.filename().string().length();
			ImGui::Text("\t%s", pwad_path.c_str() + pwad_offset);
		}

		ImGui::NewLine();

		ImGui::ListBox("IWAD List", &current_iwad_index, path_string_getter,
				available_iwad_paths.data(), available_iwad_paths.size());
		if (ImGui::Button("Refresh IWAD List"))
			available_iwad_paths = list_iwads();
		if (ImGui::Button("Add IWAD")) {
			add_iwad();
			available_iwad_paths = list_iwads();
		}
		if (ImGui::Button("Activate Selected IWAD")) {
			iwad_path = available_iwad_paths[current_iwad_index];
			available_iwad_paths = list_iwads();
		}

		ImGui::NewLine();

		std::ptrdiff_t iwad_offset = iwad_path.string().length() -
			iwad_path.filename().string().length();
		ImGui::Text("Active IWAD: %s",
				iwad_path.empty() ? "<unset>" : iwad_path.c_str() + iwad_offset);

		ImGui::NewLine();
		
		if (ImGui::Button("OK")) {
			path target_instance = available_instance_paths[current_instance_index]; 
			memset(new_instance_name, 0, sizeof(new_instance_name));
			std::size_t length = target_instance.string().length() -
				target_instance.filename().string().length();
			strncpy(new_instance_name,
					available_instance_paths[current_instance_index].c_str() + length,
					target_instance.filename().string().length());
			save_instance();
			available_instance_paths = list_instances();
			current_view = MANAGER_LAUNCHER_VIEW;
		}

		if (ImGui::Button("Apply")) {
			path target_instance = available_instance_paths[current_instance_index]; 
			memset(new_instance_name, 0, sizeof(new_instance_name));
			std::size_t length = target_instance.string().length() -
				target_instance.filename().string().length();
			strncpy(new_instance_name,
					available_instance_paths[current_instance_index].c_str() + length,
					target_instance.filename().string().length());
			save_instance();
			available_instance_paths = list_instances();
		}

		ImGui::SameLine();

		if (ImGui::Button("Cancel")) current_view = MANAGER_LAUNCHER_VIEW;

		ImGui::EndTable();

		ImGui::Text("%s", signature());

	}

	void process() {
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
		ImGui::SetNextWindowPos(ImVec2{0,0});
		ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
		ImGui::Begin("MainWindow", NULL, 
				ImGuiWindowFlags_NoDecoration |
				ImGuiWindowFlags_NoResize);

		switch (current_view) {
		case MANAGER_LAUNCHER_VIEW:
			editor_view();
			break;
		case EDITOR_VIEW:
			manager_launcher_view();
			break;
		default:
			ImGui::TextWrapped("\"You picked a bad time to get lost, friend!\"");
			ImGui::TextWrapped("You shouldn't be here. "\
					"You should tell someone about this and what you did to end up here.");
			ImGui::TextWrapped("Also, you can press the button below to go back home.");
			if (ImGui::Button("Return")) current_view = MANAGER_LAUNCHER_VIEW;
			break;
		}

		ImGui::End();
		ImGui::PopStyleVar(1);
	}

	private:
	path rootdir;
	path iwad_path;
	std::vector<path> pwad_paths;
	std::vector<std::pair<path, bool>> available_pwad_paths;
	std::vector<path> available_instance_paths;
	std::vector<path> available_iwad_paths;
	path gzdoom_path;

	char new_instance_name[32];
	char current_idgames_file[64];

	int last_instance_index = -1;
	int current_instance_index = 0;
	int current_iwad_index = 0;

	bool close_on_launch = false;

	enum VIEW {
		MANAGER_LAUNCHER_VIEW,
		EDITOR_VIEW
	} current_view = MANAGER_LAUNCHER_VIEW;
};

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
	SDL_SetWindowMinimumSize(window, MIN_WIN_SIZE_X, MIN_WIN_SIZE_Y);
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

	bool running;

	GZDoomInstancer* instancer = new GZDoomInstancer();

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

		instancer->process();

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
