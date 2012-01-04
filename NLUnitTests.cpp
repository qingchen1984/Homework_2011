﻿#include "stdafx.h"

#include <uXray/fxlog_console.h>
#include "MMU.h"
#include "AssemblyIO.h"
#include "CommandSet_original.h"
#include "Linker.h"
#include "Logic.h"
#include "Executor.h"

int main (int argc, char** argv)
{
	Debug::System::Instance().SetTargetProperties (Debug::CreateTarget ("stderr",
																		EVERYTHING,
																		EVERYTHING),
												   &FXConLog::Instance());

	Debug::API::SetTypewideVerbosity ("MallocAllocator", Debug::E_USER);
	Debug::API::SetTypewideVerbosity ("StaticAllocator", Debug::E_USER);


	Processor::ProcessorAPI api;

	api.Attach (new ProcessorImplementation::MMU);
	api.Attach (new ProcessorImplementation::UATLinker);
	api.Attach (new ProcessorImplementation::Logic);
	api.Attach (new ProcessorImplementation::CommandSet_mkI);
	api.Attach (new ProcessorImplementation::Executor);
// 	api.Flush();

	api.Attach (new ProcessorImplementation::AsmHandler);
	FILE* read = fopen ("input.dasm", "rt");
	api.Load (read, 0);

	delete api.Reader();

	Processor::calc_t result = api.Exec();

	smsg (E_INFO, E_USER, "Testing has apparently completed OK: result %lg", result);

	delete api.MMU();
	delete api.Linker();
	delete api.LogicProvider();
	delete api.CommandSet();
	delete api.Executor();

	Debug::System::Instance().CloseTargets();
}