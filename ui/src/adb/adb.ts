/*
 * Copyright (C) 2018 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

const g_textEncoder = new TextEncoder();
const g_textDecoder = new TextDecoder();

export class Adb {
  async connect() {
    (<any>window).dev = this;  ///////

    console.debug('Connecting');
    const flt = {
      classCode: 0xFF,
      subclassCode: 0x42,
      protocolCode: 0x1,
    };
    this.dev = await(<any>navigator).usb.requestDevice({filters: [flt]});
    await this.dev.open();

    console.debug('Initializing key');
    this.key = await Adb.initKey();

    let cfgValue = -1;
    let usbIfaceNumber = -1;
    let usbAltSetting = undefined;

    // Find the right interface and endpoints from the device.
    for (const cfg of this.dev.configurations) {
      for (const ifc of cfg.interfaces) {
        for (const alt of ifc.alternates) {
          if (alt.interfaceClass == flt.classCode &&
              alt.interfaceSubclass == flt.subclassCode &&
              alt.interfaceProtocol == flt.protocolCode) {
            cfgValue = cfg.configurationValue;
            usbIfaceNumber = ifc.interfaceNumber;
            usbAltSetting = alt.alternateSetting;
            for (const ep of alt.endpoints) {
              if (ep.type == 'bulk' && ep.direction == 'in') {
                this.usbReadEp = ep.endpointNumber;
              } else if (ep.type == 'bulk' && ep.direction == 'out') {
                this.usbWriteEp = ep.endpointNumber;
              }
            }  // for(endpoints)
          }    // if (alternate)
        }      // for (interface.alternates)
      }        // for (configuration.interfaces)
    }          // for (configurations)

    console.assert(usbIfaceNumber >= 0);
    console.assert(usbAltSetting !== undefined);
    console.assert(this.usbReadEp >= 0 && this.usbWriteEp >= 0);

    await this.dev.selectConfiguration(cfgValue);
    await this.dev.claimInterface(usbIfaceNumber);
    await this.dev.selectAlternateInterface(usbIfaceNumber, usbAltSetting);

    // USB Connected, now let's authenticate.
    this.state = AdbState.AUTH_STEP1;
    const VERSION = 0x01000000;
    await this.send('CNXN', VERSION, 4096, 'host:1:UsbADB');

    for (;;)
      await this.onMessage(await this.recv());
  }

  async onMessage(msg: AdbMsg) {
    if (!this.key)
      throw Error('ADB key not initialized');
    console.debug(msg);
    if (this.state == AdbState.AUTH_STEP1 && msg.cmd == 'AUTH' &&
        msg.arg0 == AuthCmd.TOKEN) {
      console.debug('Authenticating, signing token');
      this.state = AdbState.AUTH_STEP2;
      // TODO(primiano): This is wrong and will always fail. Unfortunately
      // crypto.subtle.sign assumes the input data needs hashing, which is not the
      // case for ADB, where the 20 bytes should be treated as already hashed.
      // Here we need to wrap the token witha PKCS1 padding and then do a 
      // doPrivate() operation on the private key to sign.
      let signedToken = await window.crypto.subtle.sign(
          'RSASSA-PKCS1-v1_5', this.key.privateKey, msg.data);
      await this.send('AUTH', AuthCmd.SIGNATURE, 0, new Uint8Array(signedToken));
      return;
    }
    if (this.state == AdbState.AUTH_STEP2 && msg.cmd == 'AUTH' &&
        msg.arg0 == AuthCmd.TOKEN) {
      console.debug('Authenticating, sending pubkey');
      this.state = AdbState.AUTH_STEP3;
      const encodedPubKey = await encodePubKey(this.key.publicKey);
      await this.send('AUTH', AuthCmd.RSAPUBLICKEY, 0, encodedPubKey);
      return;
    }
    if (msg.cmd == 'CNXN') {
      this.devProps = g_textDecoder.decode(msg.data);
      console.debug('Authenticated ', this.devProps);
      this.state = AdbState.CONNECTED;
      return;
    }
    if (this.state == AdbState.CONNECTED &&
        ['OKAY', 'WRTE', 'CLSE'].indexOf(msg.cmd) >= 0) {
      const stream = this.streams.get(msg.arg1);
      if (!stream) {
        console.warn(`Received message ${msg} for unknown stream ${msg.arg1}`);
        return;
      }
      await stream.onMessage(msg);
      return;
    }

    console.error(
        `Unexpected message ${msg.toString()} in state ${this.state}}`);
  }

  async shell(cmd: string): Promise<AdbStream> {
    return await this.openStream('shell:' + cmd);
  }

  async sendFile(pathOnDevice: string, file: Blob) {
    const ss = await this.openSyncStream();
    ss.write('SEND', pathOnDevice + ',' + 0o755);
    for(let off = 0; off < file.size; ) {
      let rsize = Math.min(file.size - off, 4096);
      let data = await AsyncFileRead(file, off, rsize);
      console.assert(data.byteLength == rsize);
      off += rsize;
      console.log('chunk', off);
      await ss.write('DATA', data);
    }  
    ss.write('DONE', new Uint8Array([]));
  }

  async openSyncStream() : Promise<AdbSyncStream> {
    return new AdbSyncStream(await this.openStream('sync:'));
  }

  openStream(svc: string): Promise<AdbStream> {
    let stream = new AdbStream(this, ++this.lastStreamId);
    this.streams.set(stream.localStreamId, stream);
    console.debug(`Starting stream ${stream.localStreamId} for ${svc}`);
    this.send('OPEN', stream.localStreamId, 0, svc);
    return new Promise<AdbStream>( (resolve, _) => {
      stream.onConnect = () => { resolve(stream); };
    });

  }

  async send(
      cmd: string, arg0: number, arg1: number, data?: Uint8Array|string) {
    let raw: Uint8Array = typeof data === 'undefined' ?
        new Uint8Array([]) :
        (typeof data === 'string' ? g_textEncoder.encode(data + '\0') : data);
    console.assert(cmd.length == 4);
    let buf = new Uint8Array(ADB_MSG_SIZE);
    let dv = new DataView(buf.buffer);
    const cmdBytes: Uint8Array = g_textEncoder.encode(cmd);
    for (let i = 0; i < 4; i++)
      dv.setUint8(i, cmdBytes[i]);
    dv.setUint32(4, arg0, true);
    dv.setUint32(8, arg1, true);
    dv.setUint32(12, raw.byteLength, true);
    dv.setUint32(16, Adb.checksum(raw), true);
    dv.setUint32(20, dv.getUint32(0, true) ^ 0xFFFFFFFF, true);
    await this.sendRaw(buf);
    if (raw.byteLength > 0)
      await this.sendRaw(raw);
  }

  async recv(): Promise<AdbMsg> {
    let res = await this.recvRaw(ADB_MSG_SIZE);
    console.assert(res.status == 'ok');
    let msg = AdbMsg.FromDataView(res.data);
    let bytesLeft = msg.dataLen;
    let bytesReceived = 0;
    while (bytesLeft > 0) {
      const resp = await this.recvRaw(bytesLeft);
      for (let i = 0; i < resp.data.byteLength; i++)
        msg.data[bytesReceived++] = resp.data.getUint8(i);
      bytesLeft -= resp.data.byteLength;
    }
    console.assert(Adb.checksum(msg.data) == msg.dataChecksum);
    return msg;
  }

  static async initKey(): Promise<CryptoKeyPair> {
    const keySpec = {
      name: 'RSASSA-PKCS1-v1_5',
      modulusLength: 2048,
      publicExponent: new Uint8Array([0x01, 0x00, 0x01]),
      hash: {name: 'SHA-1'},
    };

    const pubKeyJson = localStorage.getItem('adbPubKey');
    const privKeyJson = localStorage.getItem('adbPrivKey');
    if (pubKeyJson && privKeyJson) {
      const pubKey = await crypto.subtle.importKey(
          'jwk', JSON.parse(pubKeyJson), keySpec, /*extractable=*/true, ['verify']);
      const privKey = await crypto.subtle.importKey(
          'jwk', JSON.parse(privKeyJson), keySpec, /*extractable=*/true, ['sign']);
      if (pubKey && privKey) {
        return {publicKey: pubKey, privateKey: privKey};
      }
    }

    let key = <CryptoKeyPair>await crypto.subtle.generateKey(
        keySpec, /*extractable=*/true, ['sign', 'verify']);
    const expPrivKey = await crypto.subtle.exportKey('jwk', key.privateKey);
    localStorage.setItem('adbPrivKey', JSON.stringify(expPrivKey));
    const expPubKey = await crypto.subtle.exportKey('jwk', key.publicKey);
    localStorage.setItem('adbPubKey', JSON.stringify(expPubKey));
    return key;
  }

  static checksum(data: Uint8Array): number {
    let res = 0;
    for (let i = 0; i < data.byteLength; i++)
      res += data[i];
    return res & 0xFFFFFFFF;
  }

  async sendRaw(buf: Uint8Array) {
    return await this.dev.transferOut(this.usbWriteEp, buf.buffer);
  }

  async recvRaw(dataLen: number): Promise<{data: DataView, status: string}> {
    return await this.dev.transferIn(this.usbReadEp, dataLen);
  }

  state: AdbState = AdbState.DISCONNECTED;
  streams = new Map<number, AdbStream>();
  lastStreamId = 0;
  dev: any;
  devProps: string = '';
  key?: CryptoKeyPair;
  usbReadEp = -1;
  usbWriteEp = -1;
}

export class AdbSyncStream {
  constructor(stream: AdbStream) {
    this.stream = stream;
    this.stream.onConnect = () => {
      if (this.onConnect)
        this.onConnect();
    }
  }

  write(syncCmd: string, arg: string|Uint8Array) {
    const rawArg = typeof arg === 'string' ? g_textEncoder.encode(arg) : arg;
    let rawCmd = g_textEncoder.encode(syncCmd);
    let lenBytes = new Uint8Array((new Uint32Array([rawArg.length])).buffer);
    this.stream.write(AdbSyncStream.join(rawCmd, lenBytes, rawArg))
  }

  static join(...pieces: Uint8Array[]): Uint8Array {
    const totLen = pieces.reduce((p, x) => p + x.byteLength, 0);
    let arr = new Uint8Array(totLen);
    let partialLen = 0;
    for (let piece of pieces) {
      arr.set(piece, partialLen);
      partialLen += piece.byteLength;
    }
    return arr;
  }

  stream: AdbStream;
  onConnect?: VoidFunction;
}

export class AdbStream {
  constructor(adb: Adb, localStreamId: number) {
    this.adb = adb;
    this.localStreamId = localStreamId;
  }

  close() {
    console.assert(this.state == AdbStreamState.CONNECTED);
    this.adb.send('CLSE', this.localStreamId, this.remoteStreamId);
    this.doClose();
  }

  async write(msg: string|Uint8Array) {
    const raw = (typeof msg == 'string') ? g_textEncoder.encode(msg) : msg;
    if (this.state == AdbStreamState.WRITING) {
      console.debug('queueing');
      this.writeQueue.push(raw);
      return;
    }
    console.assert(this.state == AdbStreamState.CONNECTED);
    await this.adb.send('WRTE', this.localStreamId, this.remoteStreamId, raw);
    this.state = AdbStreamState.WRITING;
  }


  doClose() {
    this.state = AdbStreamState.CLOSED;
    this.adb.streams.delete(this.localStreamId);
    if (this.onClose)
      this.onClose();
  }

  onMessage(msg: AdbMsg) {
    console.assert(msg.arg1 == this.localStreamId);
    if (this.state == AdbStreamState.WAITING_OKAY && msg.cmd == 'OKAY') {
      this.remoteStreamId = msg.arg0;
      this.state = AdbStreamState.CONNECTED;
      if (this.onConnect)
        this.onConnect();
      return;
    }
    if (this.state == AdbStreamState.CONNECTED && msg.cmd == 'WRTE') {
      console.debug('RX', msg.toString());
      this.adb.send('OKAY', this.localStreamId, this.remoteStreamId);
      if (this.onData)
        this.onData(msg.dataStr, msg.data);
      return;
    }
    if (this.state == AdbStreamState.WRITING && msg.cmd == 'OKAY') {
      this.state = AdbStreamState.CONNECTED;
      const queuedMsg = this.writeQueue.shift();
      console.debug('Q?', !!queuedMsg);
      if (queuedMsg !== undefined)
        this.write(queuedMsg);
      return;
    }
    if (msg.cmd == 'CLSE') {
      console.debug(`Closing stream ${this.localStreamId}`);
      this.doClose();
      return;
    }
    console.error(
        `Unexpected stream msg ${msg.toString()} in state ${this.state}`);
  }

  adb: Adb;
  localStreamId: number;
  remoteStreamId: number = -1;
  state: AdbStreamState = AdbStreamState.WAITING_OKAY;
  writeQueue: Array<Uint8Array> = [];
  onData?: AdbStreamReadCallback;
  onConnect?: VoidFunction;
  onClose?: VoidFunction;
}

interface AdbStreamReadCallback {
  (str: string, raw: Uint8Array): void;
}

const ADB_MSG_SIZE = 6 * 4;  // 6 * int32.

enum AuthCmd {
  TOKEN = 1,
  SIGNATURE = 2,
  RSAPUBLICKEY = 3,
}

enum AdbStreamState {
  WAITING_OKAY = 0,
  CONNECTED = 1,
  WRITING = 2,
  CLOSED = 3
}

enum AdbState {
  DISCONNECTED = 0,
  AUTH_STEP1 = 1,
  AUTH_STEP2 = 2,
  AUTH_STEP3 = 3,
  CONNECTED = 2,
}

class AdbMsg {
  constructor(
      cmd: string, arg0: number, arg1: number, dataLen: number,
      dataChecksum: number) {
    console.assert(cmd.length == 4);
    this.cmd = cmd;
    this.arg0 = arg0;
    this.arg1 = arg1;
    this.dataLen = dataLen;
    this.data = new Uint8Array(dataLen);
    this.dataChecksum = dataChecksum;
  }

  get dataStr() {
    return g_textDecoder.decode(this.data);
  }

  toString() {
    return `${this.cmd} [${this.arg0},${this.arg1}] ${this.dataStr}`;
  }

  static FromDataView(dv: DataView): AdbMsg {
    console.assert(dv.byteLength == ADB_MSG_SIZE);
    const cmd = g_textDecoder.decode(dv.buffer.slice(0, 4));
    const cmdNum = dv.getUint32(0, true);
    const arg0 = dv.getUint32(4, true);
    const arg1 = dv.getUint32(8, true);
    const dataLen = dv.getUint32(12, true);
    const dataChecksum = dv.getUint32(16, true);
    const cmdChecksum = dv.getUint32(20, true);
    console.assert(cmdNum == (cmdChecksum ^ 0xFFFFFFFF));
    return new AdbMsg(cmd, arg0, arg1, dataLen, dataChecksum);
  }

  cmd: string;
  arg0: number;
  arg1: number;
  data: Uint8Array;
  dataLen: number;
  dataChecksum: number;
}

function base64StringToArray(s:string) {
  let decoded = atob(s.replace(/-/g, '+').replace(/_/g, '/'));
  return [...decoded].map(char => char.charCodeAt(0));
}

// RSA Public keys are encoded in a rather unique way. It's a base64 encoded 
// struct of 524 bytes in total as follows (see libcrypto_utils/android_pubkey.c):
// typedef struct RSAPublicKey {
//   // Modulus length. This must be ANDROID_PUBKEY_MODULUS_SIZE.
//   uint32_t modulus_size_words;
//
//   // Precomputed montgomery parameter: -1 / n[0] mod 2^32
//   uint32_t n0inv;
//
//   // RSA modulus as a little-endian array.
//   uint8_t modulus[ANDROID_PUBKEY_MODULUS_SIZE];
//
//   // Montgomery parameter R^2 as a little-endian array of little-endian words.
//   uint8_t rr[ANDROID_PUBKEY_MODULUS_SIZE];
//
//   // RSA modulus: 3 or 65537
//   uint32_t exponent;
// } RSAPublicKey;
//
// However, the Montgomery params (n0inv and rr) are not really used, see comment
// in android_pubkey_decode() ("Note that we don't extract the montgomery parameters...")
async function encodePubKey(key:CryptoKey) {
  const ANDROID_PUBKEY_MODULUS_SIZE = 2048;
  const MODULUS_SIZE_BYTES = ANDROID_PUBKEY_MODULUS_SIZE / 8;
  const expPubKey = await crypto.subtle.exportKey('jwk', key);
  const nArr = base64StringToArray(<string>expPubKey.n).reverse();
  const eArr = base64StringToArray(<string>expPubKey.e).reverse();
  console.log(nArr, eArr);

  let arr = new Uint8Array(3 * 4 + 2 * MODULUS_SIZE_BYTES);
  let dv = new DataView(arr.buffer);
  dv.setUint32(0, MODULUS_SIZE_BYTES / 4, true);
  dv.setUint32(4, 0 /*n0inv*/, true);
  for (let i = 0; i < MODULUS_SIZE_BYTES; i++)
    dv.setUint8(8 + i, nArr[i]);
  for (let i = 0; i < MODULUS_SIZE_BYTES; i++)
    dv.setUint8(8 + MODULUS_SIZE_BYTES + i, 0 /*rr*/);
  for (let i = 0; i < 4; i++)
    dv.setUint8(8 + (2 * MODULUS_SIZE_BYTES) + i, eArr[i]);
  return btoa(String.fromCharCode(...new Uint8Array(dv.buffer))) + ' perfetto@webusb';
}

async function AsyncFileRead(file: Blob, off:number, len:number) : Promise<Uint8Array> {
  const fileReader = new FileReader();
  return new Promise<Uint8Array>((resolve, reject) => {
    fileReader.onerror = () => {
      fileReader.abort();
      reject(new Error('File error'));
    };

    fileReader.onload = () => {
      resolve(new Uint8Array(fileReader.result));
    };
    fileReader.readAsArrayBuffer(file.slice(off, off + len));
  });
};
