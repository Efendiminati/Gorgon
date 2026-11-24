# Orbits the attached entity around its spawn point while spinning its sprite.
# `phase` is a script global: assigned when the script runs, it keeps its value between
# Update calls because the VirtualMachine keeps the script scope alive.

using Math

phase = 0.0

function Update(self, dt)
    phase = phase + dt
    self.x = 240 + 90 * Cos(phase * 1.6)
    self.y = 200 + 26 * Sin(phase * 3.2)
    self.rotation = phase * 90
    return 0
end function
