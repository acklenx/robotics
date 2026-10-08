"use strict";

// Copy buttons on lesson pages. Kept tiny and dependency-free: a lesson page
// should open instantly on a school tablet with no network.

var copyButtons = document.querySelectorAll("[data-copy]");

function wireCopyButton(button) {
  button.addEventListener("click", function () {
    var source = document.getElementById(button.getAttribute("data-copy"));
    if (!source) {
      return;
    }
    var programText = source.textContent;
    var originalLabel = button.textContent;

    function showCopied() {
      button.textContent = "Copied \u2713";
      setTimeout(function () { button.textContent = originalLabel; }, 1400);
    }

    if (navigator.clipboard && navigator.clipboard.writeText) {
      navigator.clipboard.writeText(programText).then(showCopied, selectInstead);
    } else {
      selectInstead();
    }

    function selectInstead() {
      // No clipboard permission (or plain file://): select it so one tap-hold
      // and "copy" finishes the job.
      var range = document.createRange();
      range.selectNodeContents(source);
      var selection = window.getSelection();
      selection.removeAllRanges();
      selection.addRange(range);
      button.textContent = "Selected \u2014 copy it";
      setTimeout(function () { button.textContent = originalLabel; }, 2000);
    }
  });
}

for (var copyIndex = 0; copyIndex < copyButtons.length; copyIndex++) {
  wireCopyButton(copyButtons[copyIndex]);
}
