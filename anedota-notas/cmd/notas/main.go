package main

import (
	"flag"
	"fmt"
	"log"
	"net/http"

	"anedota-notas/internal/notes"
	"anedota-notas/internal/web"
)

func main() {
	addr := flag.String("addr", "127.0.0.1:8080", "endereço HTTP do servidor local")
	dir := flag.String("dir", "./dados-notas", "diretório onde as pastas, notas e .anedota/ serão armazenados")
	flag.Parse()

	svc, err := notes.NewService(*dir, nil)
	if err != nil {
		log.Fatalf("erro ao iniciar serviço de notas: %v", err)
	}

	status, err := svc.Status()
	if err != nil {
		log.Fatalf("erro ao consultar status do repositório: %v", err)
	}

	srv, err := web.NewServer(svc)
	if err != nil {
		log.Fatalf("erro ao iniciar servidor web: %v", err)
	}

	fmt.Printf("ANEDOTA Notas rodando em http://%s\n", *addr)
	fmt.Printf("Diretório de notas: %s\n", svc.RootDir())
	fmt.Printf("Driver de versionamento: %s\n", status.Driver)

	if err := http.ListenAndServe(*addr, srv); err != nil {
		log.Fatalf("servidor encerrado com erro: %v", err)
	}
}
