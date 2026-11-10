# lamejs 1.2.1

`lame.min.js` and `LICENSE` are taken unchanged from the npm package [lamejs 1.2.1](https://www.npmjs.com/package/lamejs)
(<https://github.com/zhuker/lamejs>), a JavaScript port of [LAME](https://lame.sourceforge.net/), under the LGPL-3.0.
sha256 of `lame.min.js`: `15d285e2587b3bdbfd18a68de6ce07cc074f7480a82c3815da2dc1c348ec6df4`.

The converter page (`index.html`, hosted as `converter.html`) loads it as a separate file to code a bank's sounds as MP3 for the
live-cache firmware (`pack_bank.js`). Its `Mp3Encoder` codes at a constant bit rate with the bit reservoir off and writes no tag,
as `pack_bank.js` needs. To replace it, put another build of the same API here.
