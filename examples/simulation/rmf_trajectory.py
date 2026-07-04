"""Write a simulated molecule trajectory to RMF and plot the tracks (PRD-005).

The photon simulator can record the trajectory of every molecule (its position each
frame). This script simulates freely diffusing molecules, saves the trajectory to an
RMF file the same way IMP does — using the standalone RMF library, so it opens in
ChimeraX / IMP tools — and plots the x-y tracks. tttrlib itself stays RMF-free; RMF is
only used here, on the Python side, to serialise the trajectory the engine produced.
"""
import matplotlib.pyplot as plt
import numpy as np
import RMF
import tttrlib

# A sample of 30 freely diffusing molecules, all starting at the origin.
sample = tttrlib.SimSystem()
species = tttrlib.SimSpecies()
species.D = 3.0                                    # µm^2/s
species.q = tttrlib.VectorDouble([20.0, 20.0])
sample.add_species(species)
sample.set_rate_matrices(tttrlib.VectorDouble([0.0]), tttrlib.VectorDouble([0.0]))
sample.set_background(tttrlib.VectorDouble([0.0, 0.0]))
sample.set_box(20.0, 20.0)
for _ in range(30):
    sample.add_fluorophore(0.0, 0.0, 0.0, 0, True)  # mobile=True -> diffuses

settings = tttrlib.SimIntegrator()
settings.dt = 0.01
settings.n_channels = 2
settings.n_ph_max = 10 ** 9
settings.max_windows = 2000
engine = tttrlib.SimEngine(
    sample, tttrlib.SimGrid.gaussian3d(0.3, 2.0, 4.0, 8.0, 0.2, 1.0),
    tttrlib.VectorSimGrid([]), settings)

# Record every molecule's position once every 100 windows, then run the simulation.
engine.set_trajectory_reporter(100)
engine.run()

# The trajectory is returned as flat, parallel arrays (one row per molecule per frame).
frame = np.asarray(engine.trajectory_frame())
particle_id = np.asarray(engine.trajectory_id())
x = np.asarray(engine.trajectory_x())
y = np.asarray(engine.trajectory_y())
z = np.asarray(engine.trajectory_z())

# Write the trajectory to an RMF file using the RMF library. We create one particle
# node per molecule under the root, then set that node's coordinates in every frame.
rmf = RMF.create_rmf_file("/tmp/tttrlib_traj.rmf3")
rmf.set_description("tttrlib photon-simulator molecule trajectory")
particles = RMF.ParticleFactory(rmf)
root = rmf.get_root_node()
nodes = {}
for f in sorted(set(frame.tolist())):
    rmf.add_frame(str(f), RMF.FRAME)
    for i in np.where(frame == f)[0]:
        mid = int(particle_id[i])
        if mid not in nodes:                       # create the node once
            node = root.add_child("m%d" % mid, RMF.REPRESENTATION)
            particles.get(node).set_static_radius(0.3)
            particles.get(node).set_static_mass(1.0)
            nodes[mid] = node
        particles.get(nodes[mid]).set_frame_coordinates(
            RMF.Vector3(float(x[i]), float(y[i]), float(z[i])))
del rmf                                            # flush + close the file
print("wrote /tmp/tttrlib_traj.rmf3")

# Plot the x-y tracks of the diffusing molecules.
fig, ax = plt.subplots(figsize=(5.5, 5.5))
for mid in np.unique(particle_id):
    m = particle_id == mid
    order = np.argsort(frame[m])
    ax.plot(x[m][order], y[m][order], "-", alpha=0.6, lw=0.8)
ax.plot(0, 0, "k+", ms=12, label="start")
ax.set_xlabel("x (µm)")
ax.set_ylabel("y (µm)")
ax.set_aspect("equal")
ax.set_title("Simulated Brownian trajectories")
ax.legend()
fig.tight_layout()
plt.show()
