#include <AdGameUtils.h>
#include "F4SE/Impl/PCH.h"

#include <RE/C/COMPILER_NAME.h>
#include <RE/C/ConcreteFormFactory.h>
#include <RE/S/Script.h>
#include <RE/S/ScriptCompiler.h>

#include <RE/C/ConsoleLog.h>

namespace Addictol
{
	bool ExecuteCommand(std::string_view a_command, RE::TESObjectREFR* a_targetRef, bool a_silent)
	{
		RE::ConsoleLog* log = RE::ConsoleLog::GetSingleton();
		RE::ScriptCompiler compiler = RE::ScriptCompiler();
		RE::ConcreteFormFactory<RE::Script>* scriptFactory = RE::ConcreteFormFactory<RE::Script>::GetFormFactory();
		RE::Script* script = scriptFactory->Create();
		RE::BSString buffer = log->buffer;

		script->SetText(a_command);
		script->CompileAndRun(&compiler, RE::COMPILER_NAME::kSystemWindow, a_targetRef);

		if (!script->header.isCompiled)
		{
			REX::INFO("ExecuteCommand: Failed to compile command: {}"sv, a_command);
			return false;
		}

		if (a_silent == true)
			log->buffer = std::move(buffer);

		delete script;
		return true;
	}
}