#ifndef CODEGENERATOR_BIP_H
#define CODEGENERATOR_BIP_H

#include "Semantico.h"   // precisa do tipo Simbolo

#include <string>
#include <vector>
#include <functional>
#include <unordered_set>
#include <cctype>
#include <sstream>

class CodeGeneratorBIP {
public:
    struct Options {
        // .data
        bool        includeDataHeader;
        bool        sortByName;
        std::string dataComment;

        // .text
        bool        includeTextHeader;
        std::string entryLabel;      // ex.: "_PRINCIPAL"
        std::string textComment;

        Options()
            : includeDataHeader(true)
            , sortByName(true)
            , dataComment(";")
            , includeTextHeader(true)
            , entryLabel("_PRINCIPAL")
            , textComment(";")
        {}
    };

    explicit CodeGeneratorBIP(const Options& opt = Options());

    // ========= .data =========
    std::string buildDataSection(const std::vector<Simbolo>& tabela) const;

    // ========= .text – API de emissão =========
    void clearText();                               // limpa buffer de texto
    void emitInstr(const std::string& instr);       // emite linha crua (NÃO usar nome 'emit' por causa do Qt)
    void emitLabel(const std::string& label);       // rótulo "L1:"
    std::string newLabel(const std::string& prefix ="L"); // gera Lx único

    // Helpers de alto nível (endereços/globais):
    void emitLoadId(const std::string& nome);       // LDI nome ; LD 0
    void emitStoreId(const std::string& nome);      // LDI nome ; STO 0

    // Aritmética (topo da pilha):
    void emitAdd();                                 // ADD
    void emitSub();                                 // SUB
    void emitMul();                                 // MUL
    void emitDiv();                                 // DIV

    // Desvios:
    void emitJmp(const std::string& label);         // JMP label
    void emitJz(const std::string& label);          // JZ label (zero → salta)

    // ========= Programa completo =========
    std::string buildTextSection() const;           // apenas a .text (com entry + HLT)
    std::string buildProgram(const std::vector<Simbolo>& tabela) const; // .data + .text

    // ========= utilitários =========
    bool emitDataToFile(const std::string& outPath,
                        const std::vector<Simbolo>& tabela,
                        std::function<void(const std::string&)> logger = nullptr) const;

private:
    Options opt_;
    std::vector<std::string> text_;   // buffer de linhas da .text
    mutable int labelCounter_ = 0;

    static bool        isGlobalDataCandidate(const Simbolo& s);
    static std::string sanitizeLabel(const std::string& s);
};

#endif // CODEGENERATOR_BIP_H
