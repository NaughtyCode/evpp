#pragma once

#ifndef DATABASE_SERVICE_INTERNAL_ACCESS
#error "db_thread.h / db_script_vm.h are internal to the database service module. \
Use database_service.h instead. \
If you are writing database-service-internal code, #define \
DATABASE_SERVICE_INTERNAL_ACCESS before including these headers."
#endif
