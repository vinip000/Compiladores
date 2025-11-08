#include "CodeGeneratorBIP.h"
#include <fstream>
#include <algorithm>

// =================== internos ===================
static inline bool isIdentChar(unsigned char c) {
    return std::isalnum(c) || c=='_' || c=='$';
}

// =================== ctor ===================
CodeGeneratorBIP::CodeGeneratorBIP(const Options& opt)
    : opt_(opt) {}

// =================== helpers estáticos ===================
std::string CodeGeneratorBIP::sanitizeLabel(const std::string& s) {
    std::string r; r.reserve(s.size());
    for (unsigned char c : s) r.push_back(isIdentChar(c) ? char(c) : '_');
    if (r.empty()) r = "sym";
    return r;
}

bool CodeGeneratorBIP::isGlobalDataCandidate(const Simbolo& s) {
    if (s.escopo != "global") return false;
    if (s.modalidade == "parametro" || s.modalidade == "funcao") return false;
    if (s.modalidade == "variavel" || s.modalidade == "vetor") return true;
    if (s.isVetor) return true;
    return false;
}

// =================== .data ===================
std::string CodeGeneratorBIP::buildDataSection(const std::vector<Simbolo>& tabela) const {
    std::vector<const Simbolo*> cand;
    cand.reserve(tabela.size());
    for (const auto& s : tabela)
        if (isGlobalDataCandidate(s)) cand.push_back(&s);

    if (opt_.sortByName) {
        std::sort(cand.begin(), cand.end(),
                  [](const Simbolo* a, const Simbolo* b){ return a->nome < b->nome; });
    }

    std::ostringstream out;
    if (opt_.includeDataHeader) out << ".data\n";

    std::unordered_set<std::string> used;
    for (const Simbolo* ps : cand) {
        const auto& s = *ps;
        std::string label = sanitizeLabel(s.nome);

        // evita duplicata simples
        int k = 1;
        while (used.count(label)) label = sanitizeLabel(s.nome) + "_" + std::to_string(k++);
        used.insert(label);

        int N = (s.modalidade == "vetor" || s.isVetor)
                    ? (s.vetorTam > 0 ? s.vetorTam : 1)
                    : 1;

        out << label << " : ";
        for (int i = 0; i < N; ++i) {
            out << "0";
            if (i+1 < N) out << " ";
        }
        out << "   " << opt_.dataComment << " " << s.tipo;
        if (N > 1) out << " [" << N << "]";
        out << "\n";
    }
    out << "\n";
    return out.str();
}

bool CodeGeneratorBIP::emitDataToFile(const std::string& outPath,
                                      const std::vector<Simbolo>& tabela,
                                      std::function<void(const std::string&)> logger) const {
    const std::string text = buildDataSection(tabela);
    std::ofstream ofs(outPath, std::ios::binary);
    if (!ofs) {
        if (logger) logger("erro: não foi possível abrir " + outPath + " para escrita");
        return false;
    }
    ofs << text;
    if (!ofs.good()) {
        if (logger) logger("erro: falha ao gravar em " + outPath);
        return false;
    }
    if (logger) logger("gerou seção .data em: " + outPath);
    return true;
}

// =================== .text – API ===================
void CodeGeneratorBIP::clearText() { text_.clear(); }

void CodeGeneratorBIP::emitInstr(const std::string& instr) { text_.push_back(instr); }

void CodeGeneratorBIP::emitLabel(const std::string& label) {
    std::ostringstream oss; oss << sanitizeLabel(label) << ":";
    text_.push_back(oss.str());
}

std::string CodeGeneratorBIP::newLabel(const std::string& prefix) {
    std::ostringstream oss; oss << prefix << (++labelCounter_);
    return oss.str();
}

// globais: LDI nome ; LD/STO 0
void CodeGeneratorBIP::emitLoadId(const std::string& nome) {
    std::string lbl = sanitizeLabel(nome);
    emitInstr("LDI " + lbl);
    emitInstr("LD 0");
}
void CodeGeneratorBIP::emitStoreId(const std::string& nome) {
    std::string lbl = sanitizeLabel(nome);
    emitInstr("LDI " + lbl);
    emitInstr("STO 0");
}

// aritmética
void CodeGeneratorBIP::emitAdd() { emitInstr("ADD"); }
void CodeGeneratorBIP::emitSub() { emitInstr("SUB"); }
void CodeGeneratorBIP::emitMul() { emitInstr("MUL"); }
void CodeGeneratorBIP::emitDiv() { emitInstr("DIV"); }

// desvios
void CodeGeneratorBIP::emitJmp(const std::string& label) {
    emitInstr("JMP " + sanitizeLabel(label));
}
void CodeGeneratorBIP::emitJz(const std::string& label) {
    emitInstr("JZ " + sanitizeLabel(label));
}

// =================== construção da .text / programa ===================
std::string CodeGeneratorBIP::buildTextSection() const {
    std::ostringstream oss;
    if (opt_.includeTextHeader) oss << ".text\n";
    oss << opt_.entryLabel << ":\n";
    for (const auto& l : text_) {
        if (!l.empty() && l.back() == ':') oss << l << "\n";
        else                               oss << "    " << l << "\n";
    }
    oss << "    HLT 0\n";
    return oss.str();
}

std::string CodeGeneratorBIP::buildProgram(const std::vector<Simbolo>& tabela) const {
    std::ostringstream oss;
    oss << buildDataSection(tabela);
    oss << buildTextSection();
    return oss.str();
}
