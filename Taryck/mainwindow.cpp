#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QDebug>
#include <QAbstractItemView>
#include <sstream>

// GALS
#include "Lexico.h"
#include "Sintatico.h"
#include "Semantico.h"
#include "LexicalError.h"
#include "SyntacticError.h"
#include "SemanticError.h"

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
}

MainWindow::~MainWindow() {
    delete ui;
}

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
        // Dispara a análise; o semântico atualiza tabelaSimbolo e emite avisos pelo logger
        sint.parse(&lex, &sem);

        // (Opcional) reforçar avisos "declarado e não usado" ao final:
        sem.verificarNaoUsados();

        // Mensagem de sucesso
        ui->Console->appendPlainText("Compilado com sucesso!");
        ui->Console->appendPlainText("Símbolos declarados:");

        // Dump textual (mantém seu comportamento atual)
        for (size_t i = 0; i < sem.tabelaSimbolo.size(); ++i) {
            const Simbolo& s = sem.tabelaSimbolo.at(i);
            std::ostringstream oss;
            oss << s; // pressupõe operator<< definido para Simbolo
            ui->Console->appendPlainText(QString::fromStdString(oss.str()));
        }

        // Atualiza a grade visual (QTableView)
        preencherTabelaSimbolos(sem.tabelaSimbolo);

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
