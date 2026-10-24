// builtins.cpp — implementation of builtins.h.

#include "builtins.h"

LibName classifyLibName(const std::string& name) {
    if (name == "printf" || name == "puts" || name == "putchar" || name == "fprintf" || name == "fputs" ||
        name == "fputc" || name == "putc" || name == "sprintf" || name == "snprintf")
        return LibName::Output;
    if (name == "exit") return LibName::Exit;
    if (name == "scanf" || name == "fscanf") return LibName::Scan;
    if (name == "fgets") return LibName::ReadLine;
    if (name == "fclose") return LibName::Close;
    if (name == "getchar" || name == "fgetc" || name == "getc" || name == "fopen") return LibName::Value;
    return LibName::None;
}

bool isLibraryName(const std::string& name) { return classifyLibName(name) != LibName::None; }

bool libSignature(const std::string& name, LibSignature& out) {
    const Type file = Type::of(Type::Kind::File);
    const Type str = Type::of(Type::Kind::String);
    const Type integer = Type::of(Type::Kind::Int);
    if (name == "getchar") {
        out = {{}, integer};
        return true;
    }
    if (name == "fgetc" || name == "getc") {
        out = {{file}, integer};
        return true;
    }
    if (name == "fopen") {
        out = {{str, str}, file};
        return true;
    }
    return false;
}

bool isLibraryConstant(const std::string& name) {
    return name == "EXIT_SUCCESS" || name == "EXIT_FAILURE" || name == "EOF";
}

long long libraryConstant(const std::string& name) {
    if (name == "EXIT_FAILURE") return 1;
    if (name == "EOF") return -1;
    return 0;
}

bool isStandardStream(const std::string& name) { return name == "stdin" || name == "stdout" || name == "stderr"; }
