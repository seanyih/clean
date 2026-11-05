// builtins.cpp — implementation of builtins.h.

#include "builtins.h"

LibName classifyLibName(const std::string& name) {
    if (name == "printf" || name == "puts" || name == "putchar" || name == "fprintf" || name == "fputs" ||
        name == "fputc" || name == "putc" || name == "sprintf" || name == "snprintf")
        return LibName::Output;
    if (name == "exit") return LibName::Exit;
    if (name == "scanf" || name == "fscanf") return LibName::Scan;
    if (name == "fgets") return LibName::ReadLine;
    if (name == "fclose" || name == "fflush") return LibName::Close;
    if (name == "getchar" || name == "fgetc" || name == "getc" || name == "fopen") return LibName::Value;
    if (name == "bv_cat" || name == "bv_ext" || name == "bv_bit" || name == "bv_zext" || name == "bv_sext" ||
        name == "bv_rotl" || name == "bv_rotr" || name == "bv_slt" || name == "bv_sle" || name == "bv_ult" ||
        name == "bv_ule" || name == "bv_sdiv" || name == "bv_sshr")
        return LibName::BitVec;
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

bool bitVecConstArg(const std::string& name, size_t k) {
    if (name == "bv_zext" || name == "bv_sext") return k == 0;
    if (name == "bv_ext") return k == 1 || k == 2;
    return false;
}

#include <map>

namespace {

// `<stdlib.h>`, `<stdio.h>`, `<stdint.h>`, `<limits.h>` (main-spec §12.2). `int` and `unsigned` are
// unbounded here (D4), but their C limits are still the numbers C programs compare against.
const std::map<std::string, std::string>& constants() {
    static const std::map<std::string, std::string> t = {
        {"EXIT_SUCCESS", "0"}, {"EXIT_FAILURE", "1"}, {"EOF", "-1"},
        {"INT8_MIN", "-128"}, {"INT8_MAX", "127"}, {"UINT8_MAX", "255"},
        {"INT16_MIN", "-32768"}, {"INT16_MAX", "32767"}, {"UINT16_MAX", "65535"},
        {"INT32_MIN", "-2147483648"}, {"INT32_MAX", "2147483647"}, {"UINT32_MAX", "4294967295"},
        {"INT64_MIN", "-9223372036854775808"}, {"INT64_MAX", "9223372036854775807"},
        {"UINT64_MAX", "18446744073709551615"},
        {"INT_MIN", "-2147483648"}, {"INT_MAX", "2147483647"}, {"UINT_MAX", "4294967295"},
        {"CHAR_BIT", "8"},
    };
    return t;
}

}  // namespace

bool isLibraryConstant(const std::string& name) { return constants().count(name) != 0; }

std::string libraryConstant(const std::string& name) { return constants().at(name); }

namespace {

struct StdRow {
    StdShape shape;
    const char* lean;  // the Lean function (Math*/FloatTest: the part after `Float.` / `Float32.`)
};

const std::map<std::string, StdRow>& stdTable() {
    static const std::map<std::string, StdRow> t = [] {
        std::map<std::string, StdRow> m;
        // <math.h> (§12.1): all confirmed in the 4.35 reference; the `…f` variants are `Float32.*`
        for (const char* f : {"sqrt", "cbrt", "exp", "exp2", "log", "log2", "log10", "sin", "cos", "tan",
                              "asin", "acos", "atan", "sinh", "cosh", "tanh", "asinh", "acosh", "atanh",
                              "floor", "ceil", "round"}) {
            m[f] = {StdShape::Math1, f};
            m[std::string(f) + "f"] = {StdShape::Math1, f};
        }
        m["fabs"] = {StdShape::Math1, "abs"};
        m["fabsf"] = {StdShape::Math1, "abs"};
        for (const char* f : {"pow", "atan2"}) {
            m[f] = {StdShape::Math2, f};
            m[std::string(f) + "f"] = {StdShape::Math2, f};
        }
        m["isnan"] = {StdShape::FloatTest, "isNaN"};
        m["isinf"] = {StdShape::FloatTest, "isInf"};
        m["isfinite"] = {StdShape::FloatTest, "isFinite"};
        // <stdlib.h> (§12.2) and the min/max macro idiom
        for (const char* f : {"abs", "labs", "llabs"}) m[f] = {StdShape::Abs, "Int.natAbs"};
        m["min"] = {StdShape::MinMax, "min"};
        m["max"] = {StdShape::MinMax, "max"};
        // <string.h> (§12.3)
        m["strlen"] = {StdShape::StrLen, "String.length"};
        m["strcmp"] = {StdShape::StrCmp, "strcmp"};
        m["strcat"] = {StdShape::StrCat, "++"};
        m["strcpy"] = {StdShape::StrCpy, ":="};
        // <ctype.h> (§12.4)
        m["isdigit"] = {StdShape::CharTest, "Char.isDigit"};
        m["isalpha"] = {StdShape::CharTest, "Char.isAlpha"};
        m["isalnum"] = {StdShape::CharTest, "Char.isAlphanum"};
        m["isspace"] = {StdShape::CharTest, "Char.isWhitespace"};
        m["isupper"] = {StdShape::CharTest, "Char.isUpper"};
        m["islower"] = {StdShape::CharTest, "Char.isLower"};
        m["toupper"] = {StdShape::CharMap, "Char.toUpper"};
        m["tolower"] = {StdShape::CharMap, "Char.toLower"};
        // <assert.h> (§12.5)
        m["assert"] = {StdShape::Assert, "assert!"};
        return m;
    }();
    return t;
}

}  // namespace

StdShape stdShape(const std::string& name) {
    auto it = stdTable().find(name);
    return it == stdTable().end() ? StdShape::None : it->second.shape;
}

bool stdFloat32(const std::string& name) {
    const StdShape s = stdShape(name);
    return (s == StdShape::Math1 || s == StdShape::Math2) && name.back() == 'f' && name != "fabs" &&
           stdTable().count(name.substr(0, name.size() - 1));
}

std::string stdSpelling(const std::string& name, bool float32) {
    const StdRow& r = stdTable().at(name);
    if (r.shape == StdShape::Math1 || r.shape == StdShape::Math2 || r.shape == StdShape::FloatTest)
        return std::string(float32 ? "Float32." : "Float.") + r.lean;
    return r.lean;
}

bool isStandardStream(const std::string& name) { return name == "stdin" || name == "stdout" || name == "stderr"; }
