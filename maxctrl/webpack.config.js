const path = require('path');

module.exports = {
  target: 'node',
  mode: 'production',
  entry: './maxctrl.js',
  output: {
    filename: 'maxctrl.js',
    path: path.resolve(__dirname, 'dist'),
  },
  resolve: {
    fallback: {
      assert: require.resolve("assert"),
      fs: false,
      path: require.resolve("path-browserify")
    },
  },
};
