const $ = (id) => document.getElementById(id);

function compute(a, b, op) {
  switch (op) {
    case "+": return a + b;
    case "-": return a - b;
    case "*": return a * b;
    case "/": return b === 0 ? NaN : a / b;
    default: return NaN;
  }
}

$("calc").addEventListener("click", () => {
  const a = Number($("a").value);
  const b = Number($("b").value);
  const op = $("op").value;

  const r = compute(a, b, op);
  $("result").textContent = Number.isFinite(r) ? String(r) : "Error";
});

