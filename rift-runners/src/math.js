// Copyright 2026 Aditya Gudal. SPDX-License-Identifier: Apache-2.0
export function multiply(a, b) {
  const out = new Float32Array(16);
  for (let c = 0; c < 4; c++) for (let r = 0; r < 4; r++) {
    out[c * 4 + r] = a[r] * b[c * 4] + a[4 + r] * b[c * 4 + 1] + a[8 + r] * b[c * 4 + 2] + a[12 + r] * b[c * 4 + 3];
  }
  return out;
}
export function perspective(fov, aspect, near, far) {
  const f = 1 / Math.tan(fov / 2), nf = 1 / (near - far);
  return new Float32Array([f / aspect, 0, 0, 0, 0, f, 0, 0, 0, 0, (far + near) * nf, -1, 0, 0, 2 * far * near * nf, 0]);
}
export function lookAt(eye, target) {
  const z = eye.map((v, i) => v - target[i]); let n = Math.hypot(...z); for (let i = 0; i < 3; i++) z[i] /= n;
  const x = [z[2], 0, -z[0]]; n = Math.hypot(...x); for (let i = 0; i < 3; i++) x[i] /= n;
  const y = [z[1] * x[2] - z[2] * x[1], z[2] * x[0] - z[0] * x[2], z[0] * x[1] - z[1] * x[0]];
  return new Float32Array([x[0], y[0], z[0], 0, x[1], y[1], z[1], 0, x[2], y[2], z[2], 0,
    -x.reduce((a, v, i) => a + v * eye[i], 0), -y.reduce((a, v, i) => a + v * eye[i], 0), -z.reduce((a, v, i) => a + v * eye[i], 0), 1]);
}
export function project(matrix, x, y, z) {
  const w = matrix[3] * x + matrix[7] * y + matrix[11] * z + matrix[15];
  return { x: (matrix[0] * x + matrix[4] * y + matrix[8] * z + matrix[12]) / w,
    y: (matrix[1] * x + matrix[5] * y + matrix[9] * z + matrix[13]) / w, visible: w > 0 };
}

export function unprojectPlane(matrix, nx, ny, z = -55) {
  const a=matrix[0]-nx*matrix[3],b=matrix[4]-nx*matrix[7];
  const c=matrix[1]-ny*matrix[3],d=matrix[5]-ny*matrix[7];
  const rx=nx*(matrix[11]*z+matrix[15])-(matrix[8]*z+matrix[12]);
  const ry=ny*(matrix[11]*z+matrix[15])-(matrix[9]*z+matrix[13]);
  const det=a*d-b*c;
  if(Math.abs(det)<1e-9)return{x:0,y:0};
  return{x:(rx*d-b*ry)/det,y:(a*ry-rx*c)/det};
}
