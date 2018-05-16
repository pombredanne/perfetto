const { spawn } = require('child_process');

// Print without added new line.
const print = data => process.stdout.write(data);
const printErr = data => process.stderr.write(data);

const ninjaOutDir = process.env.NINJA_OUT_DIR;
let ninjaRunning = false;

module.exports = function(bs) {
  return {
    files: [
      {
        // TODO: Update this file list. This is only an example.
        match: ["ui/**", "src/trace_processor/**", "protos/**"],
        fn: function(event, file) {
          console.log(`Change detected on ${file}`);
          if (ninjaRunning) {
            console.log("Already have a ninja build running. Doing nothing.");
            return;
          }

          ninjaRunning = true;

          console.log(`Executing: ninja -C ${ninjaOutDir} ui`);
          const ninja = spawn('ninja', ['-C', ninjaOutDir, 'ui']);
          ninja.stdout.on('data', data => print(data.toString()));
          ninja.stderr.on('data', data => printErr(data.toString()));

          // We can be smarter and load just the file we need. Need to
          // resolve to the dist/location of the file in that case.
          // For now, we're reloading the whole page.
          ninja.on('exit', () => {
            ninjaRunning = false;
            bs.reload();
          });
        },
        options: {
          ignored: ["ui/dist/", "ui/.git/", "ui/node_modules/"],
          ignoreInitial: true
        }
      }
    ],
    server: {
      baseDir: "ui/dist"
    },
  };
};
