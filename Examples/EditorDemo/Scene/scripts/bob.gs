# Bobs the attached entity up and down around its spawn height. A second script drives its own
# entity independently of orbit.gs.

using Math

phase = 1.5

function Update(self, dt)
    phase = phase + dt
    self.y = 250 + 18 * Sin(phase * 2.0)
    return 0
end function
