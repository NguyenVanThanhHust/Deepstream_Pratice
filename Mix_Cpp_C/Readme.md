https://stackoverflow.com/questions/496664/c-dynamic-shared-library-on-linux

g++ -fPIC -shared  cpp_function.cpp -o libcpp_function.so

gcc main.c -L. -lcpp_function -lstdc++ -o main_c 

export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/workspace/Deepstream_Pratice/Mix_Cpp_C/

./main_c