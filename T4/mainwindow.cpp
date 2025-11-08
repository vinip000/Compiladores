#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QDebug>
#include <QAbstractItemView>
#include <QPlainTextEdit>
#include <QDockWidget>
#include <QRegularExpression>
#include <QFile>
#include <sstream>

// GALS
#include "Lexico.h"
#include "Sintatico.h"
#include "Semantico.h"
#include "LexicalError.h"
#include "SyntacticError.h"
#include "SemanticError.h"

// Gerador unificado (.data + .text + buildProgram)
#include "CodeGeneratorBIP.h"

// ---------------------------------------------
// Helper: preenche a QTableView da Tabela de Símbolos
// ---------------------------------------------
void MainWindow::preencherTabelaSimbolos(const std::vector<Simbolo>& tabela)
{
    // limpa conteúdo anterior
    modelSimbolos->removeRows(0, modelSimbolos->rowCount());

    // ajusta o número de linhas
    const int n = static_cast<int>(tabela.size());
    modelSimbolos->setRowCount(n);

    // preenche linha a linha
    for (int i = 0; i < n; ++i) {
        const auto& s = tabela[i];

        // Espera-se que Simbolo possua: nome, tipo, modalidade, escopo, usado, inicializado
        modelSimbolos->setItem(i, 0, new QStandardItem(QString::fromStdString(s.nome)));
        modelSimbolos->setItem(i, 1, new QStandardItem(QString::fromStdString(s.tipo)));
        modelSimbolos->setItem(i, 2, new QStandardItem(QString::fromStdString(s.modalidade)));
        modelSimbolos->setItem(i, 3, new QStandardItem(QString::fromStdString(s.escopo)));
        modelSimbolos->setItem(i, 4, new QStandardItem(s.usado ? "sim" : "não"));
        modelSimbolos->setItem(i, 5, new QStandardItem(s.inicializado ? "sim" : "não"));
    }

    ui->tableView->resizeColumnsToContents();
}

// ---------------------------------------------
// Helper: a partir do código-fonte do editor,
// emite instruções BIP básicas na .text usando
// o CodeGeneratorBIP (unificado).
//
// Padrões suportados:
//   dest = a + b;
//   dest = a - b;
//   dest = a * b;
//   dest = a / b;
//   dest = a;      (cópia)
// (todos com IDs válidos; sem literais por enquanto)
// ---------------------------------------------
static void emitirTextBasico(CodeGeneratorBIP& gen, const QString& fonteEditor)
{
    // 1) Atribuições binárias: dest = a OP b;
    //    OP ∈ { +, -, *, / }
    QRegularExpression rxBin(
        R"((\b[A-Za-z_]\w*\b)\s*=\s*(\b[A-Za-z_]\w*\b)\s*([+\-*/])\s*(\b[A-Za-z_]\w*\b)\s*;)"
        );
    auto itBin = rxBin.globalMatch(fonteEditor);
    while (itBin.hasNext()) {
        auto m = itBin.next();
        const std::string dst = m.captured(1).toStdString();
        const std::string a   = m.captured(2).toStdString();
        const QString    op   = m.captured(3);
        const std::string b   = m.captured(4).toStdString();

        gen.emitLoadId(a);  // LDI a ; LD 0
        gen.emitLoadId(b);  // LDI b ; LD 0

        if      (op == "+") gen.emitAdd();
        else if (op == "-") gen.emitSub();
        else if (op == "*") gen.emitMul();
        else if (op == "/") gen.emitDiv();

        gen.emitStoreId(dst); // LDI dst ; STO 0
    }

    // 2) Atribuições simples: dest = a;
    //    (evita capturar casos que já foram pegos como binários)
    QRegularExpression rxCopy(
        R"((\b[A-Za-z_]\w*\b)\s*=\s*(\b[A-Za-z_]\w*\b)\s*;)"
        );
    auto itCopy = rxCopy.globalMatch(fonteEditor);
    while (itCopy.hasNext()) {
        auto m = itCopy.next();

        // Se há um operador no "meio", é caso binário e já tratamos acima; ignora
        const QString captura = m.captured(0);
        if (captura.contains(QRegularExpression(R"([\+\-\*/])"))) continue;

        const std::string dst = m.captured(1).toStdString();
        const std::string a   = m.captured(2).toStdString();

        gen.emitLoadId(a);
        gen.emitStoreId(dst);
    }

    // Observação: literais (ex.: x = 5;) e vetores (v[i]) serão tratados depois
    // quando integrarmos a emissão diretamente nas ações do parser/semântico.
}

// ---------------------------------------------
// Helper: monta o texto do assembly completo (.data + .text)
// e também salva em "programa.asm"
// ---------------------------------------------
static QString gerarEExibirProgramaASM(const Semantico& sem,
                                       const QString& fonteEditor,
                                       QPlainTextEdit* destinoAsmView,
                                       std::function<void(const QString&)> logFn)
{
    CodeGeneratorBIP gen;

    // Emite a .text básica a partir do código do editor (padrões simples)
    emitirTextBasico(gen, fonteEditor);

    // Constrói o programa completo
    const std::string program = gen.buildProgram(sem.tabelaSimbolo);
    const QString asmText = QString::fromStdString(program);

    // Salva em arquivo (opcional, mas útil para testar no BipIDE)
    QFile f("programa.asm");
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        f.write(asmText.toUtf8());
        f.close();
        if (logFn) logFn("Gerado arquivo: programa.asm");
    } else {
        if (logFn) logFn("Aviso: não foi possível salvar o arquivo programa.asm");
    }

    // Exibir no painel
    if (destinoAsmView) {
        destinoAsmView->clear();
        destinoAsmView->setPlainText(asmText);
    }
    return asmText;
}

// =============================================
// MainWindow
// =============================================
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // Conecta o botão "Compilar" ao slot
    connect(ui->Compilar, &QPushButton::clicked, this, &MainWindow::tratarCliqueBotao);

    // --- Tabela de Símbolos (QTableView) ---
    modelSimbolos = new QStandardItemModel(this);
    modelSimbolos->setColumnCount(6);
    modelSimbolos->setHorizontalHeaderLabels(
        {"Nome", "Tipo", "Modalidade", "Escopo", "Usado", "Inicializado"}
        );

    ui->tableView->setModel(modelSimbolos);
    ui->tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);

    // Se o .ui NÃO tiver um QPlainTextEdit chamado "Asm", criamos um Dock "ASM"
    if (!this->findChild<QPlainTextEdit*>("Asm")) {
        auto *dockAsm = new QDockWidget(tr("ASM"), this);
        dockAsm->setObjectName("dockAsm");
        dockAsm->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

        auto *asmView = new QPlainTextEdit(dockAsm);
        asmView->setObjectName("asmView");
        asmView->setReadOnly(true);
        dockAsm->setWidget(asmView);

        addDockWidget(Qt::RightDockWidgetArea, dockAsm);
    }
}

MainWindow::~MainWindow() {
    delete ui;
}

void MainWindow::tratarCliqueBotao()
{
    // Limpa a saída anterior
    ui->Console->clear();
    modelSimbolos->removeRows(0, modelSimbolos->rowCount());

    // Lê o código-fonte de Entrada (QPlainTextEdit no .ui)
    const QString fonte = ui->Entrada->toPlainText();
    if (fonte.trimmed().isEmpty()) {
        ui->Console->appendPlainText("Nada para compilar.");
        return;
    }

    // Instancia o pipeline GALS
    Lexico    lex;      // constrói vazio
    Sintatico sint;
    Semantico sem;

    // alimenta o léxico com o código-fonte
    const QByteArray fonteUtf8 = fonte.toUtf8();   // mantém buffer vivo neste escopo
    lex.setInput(fonteUtf8.constData());

    // Logger: envia avisos/erros semânticos para o Console
    sem.setLogger([this](const std::string& msg) {
        ui->Console->appendPlainText(QString::fromStdString(msg));
    });

    try {
        // Dispara a análise
        sint.parse(&lex, &sem);

        // Marcar 'main' como usada (ponto de entrada)
        for (auto& s : sem.tabelaSimbolo) {
            if (s.nome == "main" && s.modalidade == "funcao") {
                s.usado = true;
                break;
            }
        }

        // Avisos finais (opcional)
        sem.verificarNaoUsados();

        // Mensagem de sucesso
        ui->Console->appendPlainText("Compilado com sucesso!");
        ui->Console->appendPlainText("Símbolos declarados:");

        // Dump textual
        for (size_t i = 0; i < sem.tabelaSimbolo.size(); ++i) {
            const Simbolo& s = sem.tabelaSimbolo.at(i);
            std::ostringstream oss;
            oss << s; // operator<< de Simbolo
            ui->Console->appendPlainText(QString::fromStdString(oss.str()));
        }

        // Atualiza a grade visual (QTableView)
        preencherTabelaSimbolos(sem.tabelaSimbolo);

        // --------- NOVO: gerar e exibir ASM (.data + .text) e salvar programa.asm ---------
        QPlainTextEdit* asmUi = this->findChild<QPlainTextEdit*>("Asm");
        if (!asmUi) {
            if (auto *dock = this->findChild<QDockWidget*>("dockAsm")) {
                asmUi = dock->findChild<QPlainTextEdit*>("asmView");
            }
        }

        gerarEExibirProgramaASM(sem, fonte, asmUi,
                                [this](const QString& m){ ui->Console->appendPlainText(m); });

        qDebug() << "Compilado com sucesso";
    }
    catch (const LexicalError &err) {
        ui->Console->appendPlainText(
            QString("Erro Léxico: %1 - posição: %2")
                .arg(toQString(err.getMessage()))
                .arg(err.getPosition()));
    }
    catch (const SyntacticError &err) {
        ui->Console->appendPlainText(
            QString("Erro Sintático: %1 - posição: %2")
                .arg(toQString(err.getMessage()))
                .arg(err.getPosition()));
    }
    catch (const SemanticError &err) {
        ui->Console->appendPlainText(
            QString("Erro Semântico: %1 - posição: %2")
                .arg(toQString(err.getMessage()))
                .arg(err.getPosition()));
    }
}
