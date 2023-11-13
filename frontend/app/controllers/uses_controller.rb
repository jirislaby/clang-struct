class UsesController < ApplicationController
#  def index
#    @uses = Use.joins(:member).limit(100);
#
#    respond_to do |format|
#      format.html
#    end
#  end

  def show
    @member = Member.joins(:struct).find(params[:id])
    @uses = Use.where(member: @member).joins(:member, :source)

    respond_to do |format|
      format.html
    end
  end

end
