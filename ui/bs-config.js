const { spawn } = require('child_process');

// Print without added new line.
const print = data => process.stdout.write(data);
const printErr = data => process.stderr.write(data);

const ninjaOutDir = process.env.NINJA_OUT_DIR;

module.exports = function(bs) {
  return {
    files: [
      {
        // TODO: Update this file list. This is only an example.
        match: ["ui/*", "src/trace_processor/*"],
        fn: function(event, file) {
          console.log(`Change detected on ${file}`);
          console.log(`Executing: ninja -C ${ninjaOutDir} ui`);

          const ninja = spawn('ninja', ['-C', ninjaOutDir, 'ui']);
          ninja.stdout.on('data', data => print(data.toString()));
          ninja.stderr.on('data', data => printErr(data.toString()));

          // TODO: We can be smarter and load just the file we need. Need to
          // resolve to the dist/location of the file in that case.
          ninja.on('exit', () => bs.reload());
        },
        options: {
          ignored: "dist/",
          ignoreInitial: true
        }
      }
    ],
    server: {
      baseDir: "ui/dist"
    }
  };
};
