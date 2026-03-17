#include <AdGameUtils.h>
#include "F4SE/Impl/PCH.h"

#include <RE/C/COMPILER_NAME.h>
#include <RE/C/ConcreteFormFactory.h>
#include <RE/S/Script.h>
#include <RE/S/ScriptCompiler.h>

// Causes crashes if put above RE/S/Script.h
#include <RE/C/ConsoleLog.h>

namespace Addictol
{
	bool ExecuteCommand(std::string_view a_command, RE::TESObjectREFR *a_targetRef, bool a_silent)
	{
		if (a_command.empty())
			return false;

		RE::ScriptCompiler compiler = RE::ScriptCompiler();
		RE::ConcreteFormFactory<RE::Script> *scriptFactory = RE::ConcreteFormFactory<RE::Script>::GetFormFactory();
		RE::Script *script = scriptFactory->Create();

		script->SetText(a_command);
		script->CompileAndRun(&compiler, RE::COMPILER_NAME::kSystemWindow, a_targetRef);

		if (!script->header.isCompiled)
		{
			REX::INFO("ExecuteCommand: Failed to compile command: {}"sv, a_command);
			return false;
		}

		if (a_silent == true)
		{
			RE::ConsoleLog *log = RE::ConsoleLog::GetSingleton();
			RE::BSString buffer = log->buffer;
			log->buffer = std::move(buffer);
		}

		delete script;
		return true;
	}

	std::string GetFormInfo(RE::TESForm *a_form)
	{
		if (!a_form)
		{
			return "{ERROR_NULL_FORM}";
		}

		RE::TESFile *file = a_form->GetFile(0);
		std::string_view editorID = a_form->GetFormEditorID();
		return std::format("{{FormID: {:08X}, EditorID: \"{}\", Plugin: \"{}\"}}"sv,
						   a_form->GetFormID(),
						   (editorID != ""sv) ? editorID : "EDITORID_NOT_LOADED"sv,
						   file ? file->GetFilename() : "PLUGIN_NOT_FOUND"sv);
	}
}