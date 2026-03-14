# CMake generated Testfile for 
# Source directory: D:/My Code/Projects/distributed-db
# Build directory: D:/My Code/Projects/distributed-db/build
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(StorageTest "D:/My Code/Projects/distributed-db/build/bin/test_storage.exe")
set_tests_properties(StorageTest PROPERTIES  _BACKTRACE_TRIPLES "D:/My Code/Projects/distributed-db/CMakeLists.txt;63;add_test;D:/My Code/Projects/distributed-db/CMakeLists.txt;0;")
add_test(HashRingTest "D:/My Code/Projects/distributed-db/build/bin/test_hash_ring.exe")
set_tests_properties(HashRingTest PROPERTIES  _BACKTRACE_TRIPLES "D:/My Code/Projects/distributed-db/CMakeLists.txt;72;add_test;D:/My Code/Projects/distributed-db/CMakeLists.txt;0;")
