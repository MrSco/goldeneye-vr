const DB_NAME = "gevr-menu-studio-v1";
let database;
async function db() {
  if (!database) database = new Promise((resolve, reject) => {
    if (!globalThis.indexedDB) return reject(new Error("Browser storage is unavailable."));
    const request = indexedDB.open(DB_NAME, 1);
    request.onupgradeneeded = () => request.result.createObjectStore("projects");
    request.onerror = () => reject(request.error);
    request.onsuccess = () => resolve(request.result);
  });
  return database;
}
export async function loadSaved() {
  const database = await db();
  return new Promise((resolve, reject) => {
    const request = database.transaction("projects", "readonly").objectStore("projects").get("current");
    request.onsuccess = () => resolve(request.result || null);
    request.onerror = () => reject(request.error);
  });
}
export async function saveProject(project) {
  const database = await db();
  return new Promise((resolve, reject) => {
    const transaction = database.transaction("projects", "readwrite");
    transaction.objectStore("projects").put(project, "current");
    transaction.oncomplete = resolve;
    transaction.onerror = () => reject(transaction.error);
    transaction.onabort = () => reject(transaction.error);
  });
}
export function downloadFile(name, content, type = "application/json") {
  const url = URL.createObjectURL(new Blob([content], { type }));
  const link = document.createElement("a");
  link.href = url; link.download = name; link.click();
  setTimeout(() => URL.revokeObjectURL(url), 10000);
}
export async function embedAssets(project) {
  const result = structuredClone(project);
  for (const asset of result.assets) if (["image","font"].includes(asset.kind) && asset.data?.startsWith("/repo-assets/")) {
    const response = await fetch(asset.data);
    if (!response.ok) throw new Error("Could not embed bundled asset: " + asset.name);
    asset.data = await readDataURL(await response.blob());
  }
  return result;
}
export function readDataURL(file) {
  return new Promise((resolve, reject) => {
    const reader = new FileReader();
    reader.onload = () => resolve(reader.result);
    reader.onerror = () => reject(reader.error);
    reader.readAsDataURL(file);
  });
}
