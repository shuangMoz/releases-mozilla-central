/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

Cu.import("resource://gre/modules/FileUtils.jsm");
Cu.import("resource://services-common/utils.js");
Cu.import("resource://gre/modules/osfile.jsm")

function run_test() {
  initTestLogging();
  run_next_test();
}

add_test(function test_roundtrip() {
  _("Do a simple write of an array to json and read");
  CommonUtils.jsonSave("foo", {}, ["v1", "v2"], ensureThrows(function(error) {
    do_check_eq(error, null);

    CommonUtils.jsonLoad("foo", {}, ensureThrows(function(val) {
      let foo = val;
      do_check_eq(typeof foo, "object");
      do_check_eq(foo.length, 2);
      do_check_eq(foo[0], "v1");
      do_check_eq(foo[1], "v2");
      run_next_test();
    }));
  }));
});

add_test(function test_string() {
  _("Try saving simple strings");
  CommonUtils.jsonSave("str", {}, "hi", ensureThrows(function(error) {
    do_check_eq(error, null);

    CommonUtils.jsonLoad("str", {}, ensureThrows(function(val) {
      let str = val;
      do_check_eq(typeof str, "string");
      do_check_eq(str.length, 2);
      do_check_eq(str[0], "h");
      do_check_eq(str[1], "i");
      run_next_test();
    }));
  }));
});

add_test(function test_number() {
  _("Try saving a number");
  CommonUtils.jsonSave("num", {}, 42, ensureThrows(function(error) {
    do_check_eq(error, null);

    CommonUtils.jsonLoad("num", {}, ensureThrows(function(val) {
      let num = val;
      do_check_eq(typeof num, "number");
      do_check_eq(num, 42);
      run_next_test();
    }));
  }));
});

add_test(function test_nonexistent_file() {
  _("Try loading a non-existent file.");
  CommonUtils.jsonLoad("non-existent", {}, ensureThrows(function(val) {
    do_check_eq(val, undefined);
    run_next_test();
  }));
});

add_test(function test_save_logging() {
  _("Verify that writes are logged.");
  let trace;
  CommonUtils.jsonSave("log", {_log: {trace: function(msg) { trace = msg; }}},
                       "hi", ensureThrows(function () {
    do_check_true(!!trace);
    run_next_test();
  }));
});

add_test(function test_load_logging() {
  _("Verify that reads and read errors are logged.");

  // Write a file with some invalid JSON
  let filePath = "log.json";
  let file = FileUtils.getFile("ProfD", filePath.split("/"), true);
  let fos = Cc["@mozilla.org/network/file-output-stream;1"]
              .createInstance(Ci.nsIFileOutputStream);
  let flags = FileUtils.MODE_WRONLY | FileUtils.MODE_CREATE
              | FileUtils.MODE_TRUNCATE;
  fos.init(file, flags, FileUtils.PERMS_FILE, fos.DEFER_OPEN);
  let stream = Cc["@mozilla.org/intl/converter-output-stream;1"]
                 .createInstance(Ci.nsIConverterOutputStream);
  stream.init(fos, "UTF-8", 4096, 0x0000);
  stream.writeString("invalid json!");
  stream.close();

  let trace, debug;
  let obj = {
    _log: {
      trace: function(msg) {
        trace = msg;
      },
      debug: function(msg) {
        debug = msg;
      }
    }
  };
  CommonUtils.jsonLoad("log", obj, ensureThrows(function(val) {
    do_check_true(!val);
    do_check_true(!!trace);
    do_check_true(!!debug);
    run_next_test();
  }));
});

add_test(function test_writeJSON_readJSON() {
  _("Round-trip some JSON through the promise-based JSON writer.");

  let contents = {
    "a": 12345.67,
    "b": {
      "c": "héllö",
    },
    "d": undefined,
    "e": null,
  };

  function checkJSON(json) {
    do_check_eq(contents.a, json.a);
    do_check_eq(contents.b.c, json.b.c);
    do_check_eq(contents.d, json.d);
    do_check_eq(contents.e, json.e);
    run_next_test();
  };

  function doRead() {
    CommonUtils.readJSON(path)
               .then(checkJSON, do_throw);
  }

  let path = OS.Path.join(OS.Constants.Path.profileDir, "bar.json");
  CommonUtils.writeJSON(contents, path)
             .then(doRead, do_throw);
});
