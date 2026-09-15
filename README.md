# ANEDOTA
API de versionamento

---

## Alunos:

- Vitor Martins Bispo
- Bruno Yozo Ricci Assakawa
- Julio Cesar Carvalho de Paula Souza.

---

## 🔎Visão Geral

O ANEDOTA é uma biblioteca/API de versionamento que pode ser utilizada por outros softwares para armazenar e consultar versões de seus dados. Seu objetivo é oferecer uma implementação menor e reutilizável das operações básicas de versionamento.

## 🎯 Objetivo

Permitir que um desenvolvedor inicialize um armazenamento de versionamento, transforme dados em objetos, crie versões, consulte o histórico e compare versões sem precisar implementar toda essa lógica em seu próprio software.

## 🚩Escopo Inicial
Fazem parte do escopo inicial: 

- inicialização de um armazenamento local;
- cálculo de identificadores por hash de conteúdo;
- armazenamento e recuperação de objetos Blob;
- representação de estruturas por objetos Tree;
- criação de objetos Commit;
- consulta do histórico;
- comparação entre versões;
- API da biblioteca;
- CLI de demonstração. 

Não fazem parte do escopo inicial a sincronização remota, o servidor, a comunicação em rede e a implementação de uma área de staging semelhante ao Index do Git. No MVP, cada commit representará um snapshot completo do estado versionado. \
No uso da CLI, a equipe poderá definir uma regra .anedotaignore para não incluir arquivos temporários ou artefatos de compilação. \
Modelagem agnóstica de dados: o núcleo manipula fluxos de bytes e objetos Blob genéricos, permitindo que a aplicação hospedeira versione desde arquivos em disco até estruturas em memória por meio da API.

## 🤝 Contribuição
- Confira as regras de contribuição desse projeto em [CONTRIBUTION.md](https://github.com/vitormbispo/trabalho-construcao-software/blob/main/CONTRIBUTION.md)
- Contribua relatando problemas e melhorias nas [issues](https://github.com/vitormbispo/trabalho-construcao-software/issues)!
