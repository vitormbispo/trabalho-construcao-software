//go:build anedota

package versioning

/*
#cgo CFLAGS: -I${SRCDIR}/../../../include -std=c99
#include <stdlib.h>
#include <anedota/anedota.h>
*/
import "C"

import (
	"fmt"
	"unsafe"
)

type anedotaCGODriver struct{}

func newDefaultBlobDriver() BlobDriver {
	return &anedotaCGODriver{}
}

func (d *anedotaCGODriver) Name() string {
	return "libanedota.a (driver de Blobs via CGO)"
}

func (d *anedotaCGODriver) Init(anedotaRoot string) error {
	cRoot := C.CString(anedotaRoot)
	defer C.free(unsafe.Pointer(cRoot))

	if rc := C.anedota_storage_init(cRoot); rc != 0 {
		return fmt.Errorf("anedota_storage_init falhou para %q (código %d)", anedotaRoot, int(rc))
	}
	return nil
}

func (d *anedotaCGODriver) WriteBlobFile(anedotaRoot, sourcePath string) (string, error) {
	cRoot := C.CString(anedotaRoot)
	defer C.free(unsafe.Pointer(cRoot))

	cSource := C.CString(sourcePath)
	defer C.free(unsafe.Pointer(cSource))

	var hashBuf [C.ANEDOTA_SHA256_HEX_SIZE]C.char
	if rc := C.anedota_blob_write_file(cRoot, cSource, &hashBuf[0]); rc != 0 {
		return "", fmt.Errorf("anedota_blob_write_file falhou para %q (código %d)", sourcePath, int(rc))
	}
	return C.GoString(&hashBuf[0]), nil
}

func (d *anedotaCGODriver) ReadBlob(anedotaRoot, hash string) ([]byte, error) {
	cRoot := C.CString(anedotaRoot)
	defer C.free(unsafe.Pointer(cRoot))

	cHash := C.CString(hash)
	defer C.free(unsafe.Pointer(cHash))

	var cData *C.char
	var cSize C.size_t
	if rc := C.anedota_blob_read(cRoot, cHash, &cData, &cSize); rc != 0 {
		return nil, fmt.Errorf("anedota_blob_read falhou para hash %q (código %d)", hash, int(rc))
	}
	defer C.free(unsafe.Pointer(cData))

	if cSize == 0 {
		return []byte{}, nil
	}
	return C.GoBytes(unsafe.Pointer(cData), C.int(cSize)), nil
}
