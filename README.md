# ANEDOTA
API de versionamento

---

## Alunos:

- Vitor Martins Bispo
- Bruno Yozo Ricci Assakawa
- Julio Cesar Carvalho de Paula Souza.
- Gustavo Amaro Niehues

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

## Algoritmo de hash

O ANEDOTA utilizará o **SHA-256** para calcular os identificadores dos objetos a partir do conteúdo armazenado. O valor será representado pela sua saída hexadecimal de 64 caracteres.

A decisão considera o SHA-256 um algoritmo consolidado, amplamente suportado e adequado ao MVP. Ele oferece resistência prática a colisões e identifica o mesmo conteúdo de maneira determinística, permitindo endereçar objetos pelo seu conteúdo.

Esta decisão será usada como base para a implementação da história H002. A implementação deverá calcular o hash sobre o conteúdo completo do arquivo ou fluxo de bytes recebido.

A função `anedota_sha256_file`, declarada em `include/anedota/hash.h`, calcula o SHA-256 dos bytes restantes de um arquivo aberto pela PAL. O arquivo deve ser aberto em modo binário e continua sob responsabilidade de quem chamou a função. Em caso de sucesso, a função retorna `0` e escreve o resultado hexadecimal em um buffer de pelo menos `ANEDOTA_SHA256_HEX_SIZE` bytes (incluindo o terminador nulo). Em caso de argumento inválido, erro de leitura ou entrada maior que o limite do SHA-256, retorna `-1` sem alterar o buffer. A implementação é em C e não requer uma biblioteca criptográfica externa.

## Estrutura do armazenamento

A inicialização cria a seguinte estrutura, usando as operações de diretório da PAL:

```text
.anedota/
├── objetos/
└── referencias/
    └── branches/
```

`objetos/` armazenará os objetos versionados, e `referencias/branches/` armazenará referências a branches. A API `anedota_storage_init(root_path)`, declarada em `include/anedota/storage.h`, recebe o caminho do diretório raiz a criar; por exemplo, passe `.anedota` para criar o armazenamento no diretório atual. Se a estrutura já existir, inicializar novamente tem sucesso sem remover ou sobrescrever os dados existentes; diretórios esperados ausentes são criados.

A função `anedota_blob_write_file(root_path, source_path, hash_out)`, também declarada em `include/anedota/storage.h`, lê o arquivo de origem por meio da PAL, calcula o seu SHA-256 e grava apenas o conteúdo bruto em `<root_path>/objetos/<hash>`, sem metadados do arquivo original. Em caso de sucesso, preenche `hash_out` com os 64 caracteres hexadecimais do hash e retorna `0`; em caso de erro, retorna `-1`.

O comando `anedota hash-object FILE` grava o conteúdo do arquivo no repositório `.anedota` do diretório atual e imprime o hash. É necessário inicializar o repositório antes com `anedota init`.

## 🤝 Contribuição
- Confira as regras de contribuição desse projeto em [CONTRIBUTING.md](https://github.com/vitormbispo/trabalho-construcao-software/blob/main/CONTRIBUTING.md)
- Contribua relatando problemas e melhorias nas [issues](https://github.com/vitormbispo/trabalho-construcao-software/issues)!
