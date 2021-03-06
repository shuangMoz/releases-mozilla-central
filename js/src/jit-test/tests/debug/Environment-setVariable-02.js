// The argument to setVariable can be a Debugger.Object.

var g = newGlobal('new-compartment');
var dbg = new Debugger;
var gw = dbg.addDebuggee(g);
dbg.onDebuggerStatement = function (frame) {
    frame.environment.setVariable("x", gw);
};
g.eval("var x = 1; debugger;");
assertEq(g.x, g);
