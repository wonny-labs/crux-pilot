// The project's own page script. It asks the project's own route and shows the answer as text, under the
// strict content policy: no inline script, no inline style, nothing from another origin.
const shown = document.getElementById('greeting');
fetch('/api/v1/project/greeting', { credentials: 'same-origin' })
  .then(async (response) => {
    if (response.status === 401) {
      shown.textContent = 'Sign in first, then open this page again.';
      return;
    }
    const body = await response.json();
    shown.textContent = typeof body.greeting === 'string' ? body.greeting : 'The device sent an unexpected answer.';
  })
  .catch(() => {
    shown.textContent = 'The device could not be reached.';
  });
