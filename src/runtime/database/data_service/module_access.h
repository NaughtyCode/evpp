#pragma once

// Compile-time access guard for database service internals (design §8)
//
// External code must ONLY include database_service.h.
// Internal code (db_thread.cc, db_script_vm.cc, database_service.cc) defines
// DATABASE_SERVICE_INTERNAL_ACCESS before including db_thread.h / db_script_vm.h.
//
// Pattern based on PHYSICS_INTERNAL_ACCESS from the Physics module.

#ifndef DATABASE_SERVICE_INTERNAL_ACCESS
#error \
	"db_thread.h / db_script_vm.h are internal to the database service module. \
Use database_service.h instead. \
If you are writing database-service-internal code, #define \
DATABASE_SERVICE_INTERNAL_ACCESS before including these headers."
#endif
