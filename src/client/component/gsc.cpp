#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "console.hpp"
#include "filesystem.hpp"

#include "game/game.hpp"

#include <xsk/gsc/types.hpp>
#include <xsk/gsc/interfaces/compiler.hpp>
#include <xsk/gsc/interfaces/assembler.hpp>
#include <xsk/resolver.hpp>
#include <interface.hpp>

#include <utils/hook.hpp>
#include <utils/io.hpp>
#include <utils/string.hpp>
#include <utils/compression.hpp>

namespace gsc
{
	namespace
	{
		auto compiler = ::gsc::compiler();
		auto assembler = ::gsc::assembler();

		//std::unordered_map<std::string, unsigned int> scr_main_handles;
		//std::unordered_map<std::string, unsigned int> scr_init_handles;

		game::ScriptFile script_file_;

		game::ScriptFile* load_custom_script(const char* file_name, std::string const& real_name)
		{
			memset(&script_file_, 0, sizeof(script_file_)); // we need to memset this so we can reuse i again

			// TODO: change this to h1-mod directory, i just don't have time atm
			std::string script_dir = "C:\\Program Files (x86)\\Steam\\steamapps\\common\\Call of Duty Modern Warfare Remastered\\h1-mod\\gsc";

			if (!strcmp("maps/mp/gametypes/_playercards", real_name.data()))
			{
				console::debug("[OVERRIDE] Loading maps/mp/gametypes/war...");

				filesystem::file source{"_playercards.gsc"}; // we store this in main/raw
				if (!source.exists())
				{
					console::debug("source gsc doesn't exist");
					return nullptr;
				}

				auto source_buffer = source.get_buffer();
				auto data = std::vector<uint8_t>{source_buffer.begin(), source_buffer.end()};

				try
				{
					compiler->compile(real_name, data);
					console::debug("compile finished");
				}
				catch (std::exception& e)
				{
					game::Com_Error(game::ERR_DROP, "Failed to compile '%s'.\n%s", real_name.data(), e.what());
					return nullptr;
				}

				auto assembly = compiler->output();

				assembler->assemble(real_name, assembly);

				auto* script_file_ptr = &script_file_;
				script_file_ptr->name = file_name;

				auto stack = assembler->output_stack(); // this is the uncompressed stack
				script_file_ptr->len = stack.size();

				auto script = assembler->output_script(); // this is the assembly bytecode
				script_file_ptr->bytecodeLen = script.size();

				// we compress the assembler's stack because that's the only way ProcessScript knows how to read it
				auto compressed = utils::compression::zlib::gsc_compress(stack); // this util comes from gsc-tool, could just change the dependency to add it lmfao

				// the size of the whole buffer for the bytecode assembly + compressed buffer
				size_t buffer_size = script.size() + compressed.size() + 1;

				auto* buffer = game::PMem_AllocFromSource_NoDebug(buffer_size, 4, 0, game::PMEM_SOURCE_SCRIPT);
				memset(buffer, 0, buffer_size);
				memcpy(buffer, script.data(), script.size()); // the first part of the buffer will be the bytecode assembly
				memcpy(&buffer[script.size()], compressed.data(), compressed.size()); // then, at the end of the bytecode assembly, the compressed stack buffer starts

				script_file_ptr->bytecode = &buffer[0];
				script_file_ptr->buffer = &buffer[script.size()];
				script_file_ptr->compressedLen = compressed.size();

				return script_file_ptr;
			}

			return nullptr;
		}

		game::ScriptFile* load_script(int, const char* name, int)
		{
			auto* result = game::DB_FindXAssetHeader(game::ASSET_TYPE_SCRIPTFILE, name, 1).scriptfile;

			std::string real_name{name};
			auto id = static_cast<std::uint16_t>(std::atoi(name));
			if (id)
			{
				real_name = xsk::gsc::h1::resolver::token_name(id);
			}

			console::debug("Loading script %s (%s)\n", real_name.data(), name);

			auto* script = load_custom_script(name, real_name);
			if (script)
			{
				// TODO: need to fix this whole thing. we are able to hit here, but we can't even print out data from script. is the data destroyed?

				console::debug("--------script data--------\n");
				console::debug("name is %s\n", script->name);
				console::debug("compressedLen is %d\n", script->compressedLen);
				console::debug("len is %d\n", script->len);
				console::debug("bytecodeLen is %d\n", script->bytecodeLen);
				//console::debug("buffer is %s\n", script->buffer);
				//console::debug("bytecode is %s\n", script->bytecode);
				console::debug("---------------------------\n");
				result = script;
			}

			return result;
		}

		/*
		void load_scripts(const std::string& script_dir)
		{
			console::debug("Loading scripts for '%s'\n", script_dir.data());

			const auto scripts = utils::io::list_files(script_dir);
			for (const auto& script : scripts)
			{
				console::debug("Checking script '%s'\n", script.data());

				std::string file = script;
				//file = "scripts/" + file;

				if (utils::string::ends_with(file, ".gsc"))
				{
					file = file.substr(0, file.size() - 4);
				}

				const auto* new_file = file.data();

				console::debug("Loading '%s.gsc'...\n", new_file);

				auto script_loaded = game::Scr_LoadScript(new_file);
				console::debug("game::Scr_LoadScript returned %d\n", script_loaded);
				if (!script_loaded)
				{
					console::debug("Script '%s.gsc' could not be loaded.\n", new_file);
					return;
				}

				// check for 'main' handle
				unsigned int main_handles = game::Scr_GetFunctionHandle(new_file, 0x267u);
				console::debug("main handles 1 is '%d'\n", main_handles); // returns 0
				if (main_handles)
				{
					console::debug("Found valid function handle '%s::main'.\n", file.data());
					scr_main_handles.insert_or_assign(file, main_handles);
				}

				// then check for 'init' handle
				main_handles = game::Scr_GetFunctionHandle(new_file, 0x208u);
				console::debug("main handles 2 is '%d'\n", main_handles); // returns 0
				if (main_handles)
				{
					console::debug("Found valid function handle '%s::init'.\n", file.data());
					scr_init_handles.insert_or_assign(file, main_handles);
				}

				console::debug("Loaded script '%s.gsc'.\n", file.data());
			}
		}

		void load_gametype_script_stub()
		{
			console::debug(__FUNCTION__ "\n");

			const auto map = game::Dvar_FindVar("mapname")->current.string;
			if (!strcmp(map, "mp_vlobby_room")) // don't know if InVirtualLobby works here
			{
				utils::hook::invoke<void>(0x18BC00_b);
				return;
			}

			// TODO: clear handles
			scr_main_handles.clear();
			scr_init_handles.clear();

			// check directories for scripts
			load_scripts("C:\\Program Files (x86)\\Steam\\steamapps\\common\\Call of Duty Modern Warfare Remastered\\h1-mod\\gsc");

			utils::hook::invoke<void>(0x18BC00_b);
		}

		void g_load_structs_stub()
		{
			console::debug(__FUNCTION__ "\n");

			const auto map = game::Dvar_FindVar("mapname")->current.string;
			if (!strcmp(map, "mp_vlobby_room"))
			{
				utils::hook::invoke<void>(0x458520_b);
				return;
			}

			for (auto& function_handle : scr_main_handles)
			{
				const auto* name = function_handle.first.data();

				console::debug("[" __FUNCTION__ "] Loading main handle '%s::main'...\n", name);
				unsigned int thread = game::Scr_ExecThread(function_handle.second, 0);
				console::debug("[" __FUNCTION__ "] Thread ID is %d\n", thread);
				game::RemoveRefToObject(thread);
				console::debug("[" __FUNCTION__ "] Executed handle '%s::main'.\n", name);
			}

			utils::hook::invoke<void>(0x458520_b);
		}

		void idk_lol_stub()
		{
			console::debug(__FUNCTION__ "\n");

			const auto map = game::Dvar_FindVar("mapname")->current.string;
			if (!strcmp(map, "mp_vlobby_room"))
			{
				utils::hook::invoke<void>(0x41DBC0_b);
				return;
			}

			for (auto& function_handle : scr_init_handles)
			{
				const auto* name = function_handle.first.data();

				console::debug("[" __FUNCTION__ "] Loading main handle '%s::init'...\n", name);
				auto thread = game::Scr_ExecThread(function_handle.second, 0);
				console::debug("[" __FUNCTION__ "] Thread ID is %d\n", thread);
				game::RemoveRefToObject(thread);
				console::debug("[" __FUNCTION__ "] Executed handle '%s::init'.\n", name);
			}

			utils::hook::invoke<void>(0x41DBC0_b);
		}
		*/
	}

	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			// hook scriptfile loading
			utils::hook::call(0x50E357_b, load_script); // hooking DB_FindXAssetHeader for ASSET script

			// load custom scripts
			//utils::hook::call(0x18C325_b, load_gametype_script_stub);

			// execute handles
			//utils::hook::call(0x420EA2_b, g_load_structs_stub);
			//utils::hook::call(0x420F19_b, idk_lol_stub); // execute init handles (Scr_StartupGameType is inlined)

			//utils::hook::call(0x50E357, load_script);
			//utils::hook::jump(0x479270, script_error);

			//utils::hook::call(0x4D0719, read_custom_scripts);
			//utils::hook::call(0x52FFE8, exec_custom_scripts);
		}
	};
}

REGISTER_COMPONENT(gsc::component)
