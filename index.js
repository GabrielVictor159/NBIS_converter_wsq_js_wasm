import createNbisModule from './nbis_wsq.js';

class NbisConverter {
    constructor() {
        this.module = null;
        this.inicializado = false;
    }

    /**
     * Inicializa o módulo WebAssembly.
     * @param {string} wasmPath Caminho para o arquivo .wasm (geralmente na pasta public do Vue)
     */
    async init(wasmPath = '/nbis_wsq.wasm') {
        if (this.inicializado) return;

        try {
            this.module = await createNbisModule({
                locateFile: (path) => {
                    if (path.endsWith('.wasm')) return wasmPath;
                    return path;
                }
            });
            this.inicializado = true;
            console.log("NBIS WASM: Inicializado com sucesso.");
        } catch (error) {
            console.error("NBIS WASM: Falha ao carregar o módulo.", error);
            throw error;
        }
    }

    /**
     * Converte imagens entre WSQ e JPG/PNG.
     * @param {ArrayBuffer|Uint8Array} buffer Dados da imagem de entrada
     * @param {boolean} toWsq True para converter PARA WSQ, False para converter DE WSQ
     * @param {boolean} isPng True se a entrada/saída for PNG, False para JPG
     */
    async convert(buffer, toWsq = true, isPng = false) {
        if (!this.inicializado || !this.module) {
            throw new Error("NBIS WASM: O módulo não foi inicializado. Chame .init() primeiro.");
        }

        const uint8Array = buffer instanceof Uint8Array ? buffer : new Uint8Array(buffer);
        
        const inputPtr = this.module._malloc(uint8Array.length);
        this.module.writeArrayToMemory(uint8Array, inputPtr);

        const sizePtr = this.module._malloc(4);
        
        try {
            const funcName = toWsq ? 'converter_para_wsq' : 'converter_de_wsq';
            
            const outPtr = this.module.ccall(
                funcName,
                'number',
                ['number', 'number', 'number', 'number'],
                [inputPtr, uint8Array.length, isPng ? 1 : 0, sizePtr]
            );

            if (outPtr === 0) {
                throw new Error("NBIS WASM: Falha interna na conversão (o conversor retornou nulo).");
            }

            const outSize = this.module.getValue(sizePtr, 'i32');
            
            const resultView = new Uint8Array(this.module.HEAPU8.buffer, outPtr, outSize);
            const finalData = new Uint8Array(resultView);

            this.module.ccall('liberar_memoria', null, ['number'], [outPtr]);

            return finalData;

        } catch (err) {
            console.error("NBIS WASM: Erro durante a execução.", err);
            throw err;
        } finally {
            // Sempre libera os ponteiros de entrada para evitar Memory Leaks
            if (inputPtr) this.module._free(inputPtr);
            if (sizePtr) this.module._free(sizePtr);
        }
    }
}

const instance = new NbisConverter();
export default instance;