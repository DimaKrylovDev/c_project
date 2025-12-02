const state = {
  token: localStorage.getItem('bb_token') || '',
  user: null,
};

const els = {
  registerForm: document.getElementById('registerForm'),
  loginForm: document.getElementById('loginForm'),
  adForm: document.getElementById('adForm'),
  refreshAds: document.getElementById('refreshAds'),
  adsList: document.getElementById('adsList'),
  myAdsList: document.getElementById('myAdsList'),
  message: document.getElementById('message'),
  profileLogout: document.getElementById('profileLogout'),
  closeModal: document.getElementById('closeModal'),
  accountModal: document.getElementById('accountModal'),
  backdrop: document.getElementById('backdrop'),
  tabLogin: document.getElementById('tabLogin'),
  tabRegister: document.getElementById('tabRegister'),
  loginView: document.getElementById('loginView'),
  registerView: document.getElementById('registerView'),
  showLogin: document.getElementById('showLogin'),
  showRegister: document.getElementById('showRegister'),
  authActions: document.getElementById('authActions'),
  profileActions: document.getElementById('profileActions'),
  profileSummary: document.getElementById('profileSummary'),
  openProfile: document.getElementById('openProfile'),
  profilePanel: document.getElementById('profilePanel'),
  modalProfileSummary: document.getElementById('modalProfileSummary'),
  modalTabs: document.getElementById('modalTabs'),
};

function showMessage(text, isError = false) {
  els.message.textContent = text;
  els.message.classList.remove('hidden', 'error');
  if (isError) {
    els.message.classList.add('error');
  }
  setTimeout(() => {
    els.message.classList.add('hidden');
  }, 4000);
}

function buildHeaders(contentType) {
  const headers = { Accept: 'application/json' };
  if (contentType) {
    headers['Content-Type'] = contentType;
  }
  if (state.token) {
    headers['Authorization'] = `Bearer ${state.token}`;
  }
  return headers;
}

async function handleResponse(response) {
  const text = await response.text();
  let data = {};
  if (text) {
    try {
      data = JSON.parse(text);
    } catch {
      data = {};
    }
  }
  if (!response.ok) {
    throw new Error(data.error || 'Ошибка запроса');
  }
  return data;
}

async function postForm(path, form) {
  const body = new URLSearchParams(new FormData(form));
  const response = await fetch(path, {
    method: 'POST',
    headers: buildHeaders('application/x-www-form-urlencoded'),
    body,
  });
  return handleResponse(response);
}

async function fetchJson(path, options = {}) {
  const response = await fetch(path, {
    method: options.method || 'GET',
    headers: buildHeaders(options.body ? 'application/x-www-form-urlencoded' : undefined),
    body: options.body,
  });
  return handleResponse(response);
}

async function refreshSession() {
  try {
    const data = await fetchJson('/api/session');
    state.user = data.authenticated ? data.user : null;
    updateMyAdsUI();
  } catch (error) {
    showMessage(error.message, true);
  }
}

async function loadAds() {
  try {
    const data = await fetchJson('/api/ads');
    renderAds(els.adsList, data.ads || [], false);
  } catch (error) {
    showMessage(error.message, true);
  }
}

function renderAds(listElement, ads, withActions = false) {
  if (!ads.length) {
    listElement.innerHTML = '<p class="muted">Пока нет объявлений</p>';
    return;
  }
  listElement.innerHTML = '';
  ads.forEach((ad) => {
    const card = document.createElement('article');
    card.className = 'ad';
    card.innerHTML = `
      <div class="ad-header">
        <h3>${escapeHtml(ad.title)}</h3>
        <span class="price">${Number(ad.price).toFixed(2)} ₽</span>
      </div>
      <p class="ad-description">${escapeHtml(ad.description)}</p>
      <div class="ad-meta">
        <span>Автор: ${escapeHtml(ad.ownerName)}</span>
        <span>${formatDate(ad.createdAt)}</span>
      </div>
    `;
    if (withActions) {
      const button = document.createElement('button');
      button.textContent = 'Удалить';
      button.className = 'danger';
      button.addEventListener('click', () => deleteAd(ad.id));
      card.appendChild(button);
    }
    listElement.appendChild(card);
  });
}

function escapeHtml(value) {
  const div = document.createElement('div');
  div.textContent = value;
  return div.innerHTML;
}

function formatDate(timestamp) {
  if (!timestamp) return '';
  const date = new Date(timestamp * 1000);
  return date.toLocaleString();
}

async function deleteAd(id) {
  if (!confirm('Удалить объявление?')) return;
  try {
    const response = await fetch(`/api/ads/${id}`, {
      method: 'DELETE',
      headers: buildHeaders(),
    });
    await handleResponse(response);
    showMessage('Объявление удалено');
    loadAds();
    loadMyAds();
  } catch (error) {
    showMessage(error.message, true);
  }
}

function setActiveTab(tab) {
  if (tab === 'login') {
    els.tabLogin.classList.add('active');
    els.tabRegister.classList.remove('active');
    els.loginView.classList.remove('hidden');
    els.registerView.classList.add('hidden');
    els.profilePanel.classList.add('hidden');
  } else {
    els.tabRegister.classList.add('active');
    els.tabLogin.classList.remove('active');
    els.registerView.classList.remove('hidden');
    els.loginView.classList.add('hidden');
    els.profilePanel.classList.add('hidden');
  }
}

function openModal(initialTab = 'login') {
  if (state.user) {
    els.modalTabs.classList.add('hidden');
    els.tabLogin.classList.remove('active');
    els.tabRegister.classList.remove('active');
    els.loginView.classList.add('hidden');
    els.registerView.classList.add('hidden');
    els.profilePanel.classList.remove('hidden');
    els.modalProfileSummary.textContent = `${state.user.name} · ${state.user.email}`;
    loadMyAds();
  } else {
    els.modalTabs.classList.remove('hidden');
    els.profilePanel.classList.add('hidden');
    setActiveTab(initialTab);
  }
  els.accountModal.classList.remove('hidden');
  els.backdrop.classList.remove('hidden');
}

function closeModal() {
  els.accountModal.classList.add('hidden');
  els.backdrop.classList.add('hidden');
}

els.closeModal.addEventListener('click', closeModal);
els.backdrop.addEventListener('click', closeModal);
els.tabLogin.addEventListener('click', () => setActiveTab('login'));
els.tabRegister.addEventListener('click', () => setActiveTab('register'));
els.showLogin.addEventListener('click', () => openModal('login'));
els.showRegister.addEventListener('click', () => openModal('register'));
els.openProfile.addEventListener('click', () => openModal('profile'));

els.registerForm.addEventListener('submit', async (event) => {
  event.preventDefault();
  try {
    await postForm('/api/register', event.target);
    showMessage('Регистрация выполнена успешно! Теперь войдите в систему.');
    event.target.reset();
    setActiveTab('login');
  } catch (error) {
    showMessage(error.message, true);
  }
});

els.loginForm.addEventListener('submit', async (event) => {
  event.preventDefault();
  try {
    const data = await postForm('/api/login', event.target);
    state.token = data.token;
    localStorage.setItem('bb_token', state.token);
    showMessage(`Добро пожаловать, ${data.user?.name || 'пользователь'}!`);
    event.target.reset();
    closeModal();
    await refreshSession();
  } catch (error) {
    showMessage(error.message, true);
  }
});

els.profileLogout.addEventListener('click', async () => {
  try {
    await fetchJson('/api/logout', { method: 'POST', body: new URLSearchParams() });
  } catch {
    // ignore
  } finally {
    state.token = '';
    state.user = null;
    localStorage.removeItem('bb_token');
    updateMyAdsUI();
    refreshSession();
  }
});

els.adForm.addEventListener('submit', async (event) => {
  event.preventDefault();
  if (!state.token) {
    showMessage('Необходимо войти', true);
    openModal();
    return;
  }
  try {
    await postForm('/api/ads', event.target);
    showMessage('Объявление опубликовано');
    event.target.reset();
    loadAds();
    loadMyAds();
  } catch (error) {
    showMessage(error.message, true);
  }
});

els.refreshAds.addEventListener('click', loadAds);

function updateMyAdsUI() {
  if (state.user) {
    els.authActions.classList.add('hidden');
    els.profileActions.classList.remove('hidden');
    els.profileSummary.textContent = `${state.user.name} · ${state.user.email}`;
    els.profileLogout.disabled = false;
    els.adForm.querySelectorAll('input, textarea, button').forEach((el) => {
      el.disabled = false;
    });
    loadMyAds();
  } else {
    els.profileActions.classList.add('hidden');
    els.authActions.classList.remove('hidden');
    els.profileSummary.textContent = '';
    els.profileLogout.disabled = true;
    els.adForm.querySelectorAll('input, textarea, button').forEach((el) => {
      el.disabled = true;
    });
    if (els.myAdsList) {
      els.myAdsList.innerHTML = '<p class="muted">Войдите, чтобы увидеть свои объявления</p>';
    }
  }
}

async function loadMyAds() {
  if (!state.token || !els.myAdsList) return;
  try {
    const data = await fetchJson('/api/ads');
    const myAds = (data.ads || []).filter((ad) => ad.mine);
    renderAds(els.myAdsList, myAds, true);
  } catch (error) {
    showMessage(error.message, true);
  }
}

setActiveTab('login');
updateMyAdsUI();
refreshSession();
loadAds();

